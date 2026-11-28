// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket — the web editor's protocol over WebMIDI (docs/EDITOR-PROTOCOL.md; the pedal side: firmware/px_editor.h).
// Request / reply, one at a time; STATE frames the pedal pushes while watched go to onState.

import { PORT_RE } from "./pxota.js";

const H = [0x7D, 0x4F, 0x50];
export const C = { INFO: 1, CAT_BLOCK: 2, CAT_TYPE: 3, TEXTS: 4, STATE: 5, SET: 6, ORDER: 7, NAME: 8, PUT: 9, SELECT: 10,
  FOCUS: 11, LIST: 12, SLOT_GET: 13, SLOT_PUT: 14, COMMIT: 15, SAVE: 16, WATCH: 17, PING: 18,
  DIAG: 23, USB_AUDIO: 28, FOOTSWITCH: 29, SHED: 30, GLOBAL: 31, TUNER: 32, SCENE: 33 };   // (19..22 and 24..27, the NAM models and IRs, retired in v70)
// px_preset_t: a 14-byte name, then on, type, 5 settings and the order for each block (v75: 10 blocks, 94 bytes; v70: 9, 86)
export const presetSize = (nblk = 10, npar = 5, valveBytes = 0) => 14 + nblk * (3 + npar) + valveBytes;
export const VALVE_ACTION = 10;
export const PATCH_ACTION = 11;
export const VALVE_DEFAULT = 71;
export const VALVE_LEVEL_UNITY = 24;
export const VALVE_LEVEL_MAX = 48;
const valveValue = (v, max = 100) => Math.max(0, Math.min(max, Math.round(Number(v)) || 0));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
function fail(code, msg) { const e = new Error(msg); e.code = code; return e; }

// a reply's arguments, read in order
class Reader {
  constructor(a) { this.a = a; this.i = 0; }
  b() { if (this.i >= this.a.length) throw fail("short", "reply too short"); return this.a[this.i++]; }
  s() { let t = ""; for (;;) { const c = this.b(); if (!c) return t; t += String.fromCharCode(c); } }
  bytes(n) { const r = this.a.slice(this.i, this.i + n); if (r.length < n) throw fail("short", "reply too short"); this.i += n; return r; }
}

// preset <-> the bytes of px_preset_t
export function decodePreset(b, nblk = 10, npar = 5) {
  let i = 0;
  const name = String.fromCharCode(...b.slice(0, 14)).replace(/\0.*$/s, "");
  i = 14;
  const on = [...b.slice(i, i + nblk)].map((x) => x ? 1 : 0); i += nblk;
  const type = [...b.slice(i, i + nblk)]; i += nblk;
  const p = []; for (let k = 0; k < nblk; k++) { p.push([...b.slice(i, i + npar)]); i += npar; }
  const order = [...b.slice(i, i + nblk)];
  i += nblk;
  return { name, on, type, p, order, ...(b.length >= i + 2 && { valveOn: !!b[i], valveDrive: b[i + 1] }),
    ...(b.length >= i + 3 && { valveLevel: b[i + 2] }), ...(b.length >= i + 4 && { patchVolume: b[i + 3] }) };
}
export function encodePreset(q, nblk = 10, npar = 5, valveBytes = 0) {
  const out = [];
  const nm = cleanName(q.name);
  for (let i = 0; i < 14; i++) out.push(i < nm.length ? nm.charCodeAt(i) : 0);
  for (let k = 0; k < nblk; k++) out.push(q.on[k] ? 1 : 0);
  for (let k = 0; k < nblk; k++) out.push(q.type[k] & 0x7F);
  for (let k = 0; k < nblk; k++) for (let j = 0; j < npar; j++) out.push(Math.max(0, Math.min(100, q.p[k][j] | 0)));
  for (let k = 0; k < nblk; k++) out.push(q.order[k] & 0x7F);
  if (valveBytes >= 2) out.push(q.valveOn ? 1 : 0, valveValue(q.valveDrive ?? VALVE_DEFAULT));
  if (valveBytes >= 3) out.push(valveValue(q.valveLevel ?? VALVE_LEVEL_UNITY, VALVE_LEVEL_MAX));
  if (valveBytes >= 4) out.push(valveValue(q.patchVolume ?? 50));
  return out;
}
export function cleanName(s, max = 13) {
  return String(s || "").replace(/[^\x20-\x7E]/g, "?").slice(0, max);
}
export const clonePreset = (q) => ({ ...q, on: [...q.on], type: [...q.type], p: q.p.map((r) => [...r]), order: [...q.order] });

export class Editor {
  constructor(access, opts = {}) {
    this.access = access;
    this.timeout = opts.timeout || 1500;
    this.onState = null;          // (state) => {}: pushed by the pedal
    this.onLost = null;           // () => {}: the pedal stopped answering
    this.onMidi = null;           // (bytes) => {}: a musical message the pedal sent (docs/MIDI.md), not SysEx
    this.link = null;
    this.queue = Promise.resolve();
    this.waiting = null;          // { cmd, resolve, reject, timer }
    this.info = null;
    this.cat = null;
  }

  // ---- transport ----
  _onMessage(data) {
    const d = data instanceof Uint8Array ? data : Uint8Array.from(data);
    if (d.length && d[0] !== 0xF0) { if (this.onMidi) { try { this.onMidi(d); } catch (_) { } } return; }
    if (d.length < 6 || d[0] !== 0xF0 || d[1] !== H[0] || d[2] !== H[1] || d[3] !== H[2] || d[d.length - 1] !== 0xF7) return;
    const cmd = d[4], args = d.slice(5, d.length - 1);
    if (this.waiting && this.waiting.cmd === cmd) {
      const w = this.waiting; this.waiting = null; clearTimeout(w.timer); w.resolve(args);
      return;
    }
    if (cmd === C.STATE && this.onState) { try { this.onState(this._state(args)); } catch (_) { } }
  }
  _send(cmd, args) {
    if (!this.link) throw fail("closed", "not connected");
    this.link.output.send([0xF0, ...H, cmd, ...args.map((x) => x & 0x7F), 0xF7]);
  }
  _once(cmd, args, timeout) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.waiting = null; reject(fail("timeout", "the pedal did not answer")); }, timeout);
      this.waiting = { cmd, resolve, reject, timer };
      try { this._send(cmd, args); } catch (e) { clearTimeout(timer); this.waiting = null; reject(e); }
    });
  }
  // one request at a time; one retry after a timeout
  request(cmd, args = [], timeout = this.timeout) {
    const run = async () => {
      try { return await this._once(cmd, args, timeout); }
      catch (e) {
        if (e.code !== "timeout") throw e;
        await sleep(50);
        try { return await this._once(cmd, args, timeout); }
        catch (e2) { if (e2.code === "timeout" && this.onLost) this.onLost(); throw e2; }
      }
    };
    const p = this.queue.then(run, run);
    this.queue = p.catch(() => {});
    return p;
  }

  // the first OpenPocket port that answers INFO
  async connect() {
    const outs = [...this.access.outputs.values()];
    for (const input of this.access.inputs.values()) {
      if (input.state === "disconnected" || !PORT_RE.test(input.name || "")) continue;
      const output = outs.find((o) => o.name === input.name) || (outs.length === 1 ? outs[0] : null);
      if (!output) continue;
      try { await input.open(); await output.open(); } catch (_) { continue; }
      this.link = { input, output };
      input.onmidimessage = (ev) => this._onMessage(ev.data);
      try {
        const r = new Reader(await this.request(C.INFO, [], 600));
        const info = { proto: r.b(), version: r.s(), identity: r.s(), nblk: r.b(), npar: r.b(), nslot: r.b(), nameMax: r.b(), port: input.name };
        if (info.proto >= 1) { this.info = info; return info; }
      } catch (_) { }
      input.onmidimessage = null;
      this.link = null;
    }
    return null;
  }
  // the log channel: its text lines; a timeout when this firmware has none (only the Bluetooth test image answers)
  async diag() {
    const r = new Reader(await this.request(C.DIAG, [], 600));
    return Array.from({ length: r.b() }, () => r.s());
  }
  close() {
    if (this.link) { try { this.link.input.onmidimessage = null; } catch (_) { } }
    this.link = null;
  }

  // ---- the catalogue: blocks, types, parameter names and value texts (cached per firmware) ----
  async catalog(progress = () => {}) {
    const key = `openpocket-cat-${this.info.identity}-${this.info.version}-${this.info.proto}`;
    let cached;
    try { cached = JSON.parse(localStorage.getItem(key) || "null"); } catch (_) { }
    const blocks = [];
    for (let b = 0; b < this.info.nblk; b++) {
      const r = new Reader(await this.request(C.CAT_BLOCK, [b]));
      r.b();
      const label = r.s(), name = r.s(), ntypes = r.b();
      const c = r.b() | r.b() << 7 | r.b() << 14;
      const rgb = `#${(((c >> 11) & 31) * 255 / 31 | 0).toString(16).padStart(2, "0")}${(((c >> 5) & 63) * 255 / 63 | 0).toString(16).padStart(2, "0")}${((c & 31) * 255 / 31 | 0).toString(16).padStart(2, "0")}`;
      blocks.push({ label, name, color: rgb, types: new Array(ntypes) });
    }
    // Development builds can share an identity even when a block gains a type.
    if (cached?.blocks?.length === blocks.length && blocks.every((b, i) => {
      const old = cached.blocks[i];
      return old?.label === b.label && old.name === b.name && old.color === b.color && old.types?.length === b.types.length;
    })) {
      this.cat = cached;
      return cached;
    }
    const total = blocks.reduce((s, b) => s + b.types.length, 0);
    let done = 0;
    for (let b = 0; b < blocks.length; b++) {
      for (let t = 0; t < blocks[b].types.length; t++) {
        const r = new Reader(await this.request(C.CAT_TYPE, [b, t]));
        r.b(); r.b();
        const name = r.s(), np = r.b(), mask = r.b();
        const params = [];
        for (let k = 0; k < np; k++) params.push({ name: r.s(), texts: null });
        for (let k = 0; k < np; k++) {
          if (!(mask >> k & 1)) continue;
          const q = new Reader(await this.request(C.TEXTS, [b, t, k]));
          q.b(); q.b(); q.b();
          const texts = []; for (let v = 0; v <= 100; v++) texts.push(q.s());
          params[k].texts = texts;
        }
        blocks[b].types[t] = { name, params };
        progress(++done / total);
      }
    }
    this.cat = { blocks };
    try { localStorage.setItem(key, JSON.stringify(this.cat)); } catch (_) { }
    return this.cat;
  }
  // ---- the current sound ----
  get hasSceneCount() { return this.info?.proto >= 16; }
  get hasScenes() { return this.info?.proto >= 13; }
  get hasValve() { return this.info?.proto >= 10; }
  get hasValveLevel() { return this.info?.proto >= 11; }
  get valveBytes() { return this.info?.proto >= 12 ? 4 : this.hasValveLevel ? 3 : this.hasValve ? 2 : 0; }
  _readPreset(r) {
    const { nblk, npar } = this.info;
    return decodePreset(r.bytes(presetSize(nblk, npar, this.valveBytes)), nblk, npar);
  }
  _state(args) {
    const r = new Reader(args);
    const slot = r.b(), flags = r.b(), focus = r.b();
    const state = { slot, dirty: !!(flags & 1), saved: !!(flags & 2), focus, preset: this._readPreset(r) };
    if (this.hasScenes) {
      const start = r.b(), active = r.b(), mode = !!r.b();
      const names = start ? [r.s(), r.s(), r.s()] : [];
      state.scenes = { start, active, mode, names, count: this.hasSceneCount ? r.b() : start ? 3 : 0 };
    }
    return state;
  }
  async state() { return this._state(await this.request(C.STATE)); }
  async set(block, what, k, v) { const r = await this.request(C.SET, [block, what, k, v]); return r[3]; }
  setParam(block, k, v) { return this.set(block, 0, k, v); }
  setOn(block, on) { return this.set(block, 1, 0, on ? 1 : 0); }
  setType(block, t) { return this.set(block, 2, 0, t); }
  async order(order) { return (await this.request(C.ORDER, order))[0]; }
  async name(s) { return new Reader(await this.request(C.NAME, [...cleanName(s, this.info.nameMax)].map((c) => c.charCodeAt(0)).concat([0]))).s(); }
  async put(q, replaceScenes = false) {
    const args = encodePreset(q, this.info.nblk, this.info.npar, this.valveBytes);
    if (this.hasScenes && replaceScenes) args.push(...this._sceneBytes(q));
    return (await this.request(C.PUT, args))[0];
  }
  _sceneBytes(q) {
    const bytes = [...(q.sceneData || [0])];
    if (!this.hasSceneCount) bytes[0] &= 3;
    return bytes;
  }
  async scene(op, ...args) { return (await this.request(C.SCENE, [op, ...args]))[0]; }
  async patchWithScenes() {
    const r = new Reader(await this.request(C.SCENE));
    r.b();
    return this._readPatchWithScenes(r);
  }
  _readPatchWithScenes(r) {
    const preset = this._readPreset(r);
    if (this.hasScenes) {
      const start = r.b();
      if (start) preset.sceneData = [start, ...r.bytes(3 * 10 + 2 * (6 + this.info.nblk * this.info.npar))];
    }
    return preset;
  }
  async select(slot) { return (await this.request(C.SELECT, [slot]))[1]; }
  async focus(block) { await this.request(C.FOCUS, [block]); }

  // ---- the slots ----
  async list() {
    const out = [];
    for (let s = 0; s < this.info.nslot; s += 26) {
      const r = new Reader(await this.request(C.LIST, [s, Math.min(26, this.info.nslot - s)]));
      r.b(); const n = r.b();
      for (let i = 0; i < n; i++) out.push({ slot: s + i, saved: !!r.b(), name: r.s() });
    }
    return out;
  }
  async slotGet(slot) {
    const r = new Reader(await this.request(C.SLOT_GET, [slot]));
    r.b(); const saved = !!r.b();
    return { slot, saved, preset: this._readPatchWithScenes(r) };
  }
  // q null: back to the factory preset. commit: write the flash now
  async slotPut(slot, q, commit) {
    const args = [slot, (q ? 1 : 0) | (commit ? 2 : 0)];
    if (q) {
      args.push(...encodePreset(q, this.info.nblk, this.info.npar, this.valveBytes));
      if (this.hasScenes && q.sceneData) args.push(...this._sceneBytes(q));
    }
    return (await this.request(C.SLOT_PUT, args, commit ? 4000 : this.timeout))[1];
  }
  async commit() { return (await this.request(C.COMMIT, [], 4000))[0]; }
  async save(slot) { return (await this.request(C.SAVE, [slot], 4000))[1]; }
  async watch(on) { return (await this.request(C.WATCH, [on ? 1 : 0]))[0]; }
  async ping() {
    const r = await this.request(C.PING), p = { load: r[0], bypass: !!(r[1] & 1), c1: !!(r[1] & 2), shed: !!(r[1] & 4) };   // (bit 2, v75: a block is switched off for the load)
    if (r.length >= 8) Object.assign(p, { peak: r[2], peakAll: r[3], xrun: r[4] | r[5] << 7, clip: r[6] | r[7] << 7 });   // v40.1
    if (r.length >= 12) Object.assign(p, { stackFree: (r[8] | r[9] << 7) * 4, irqStackFree: (r[10] | r[11] << 7) * 4 });
    // v40.2: then a count and that many 2-byte figures (the DSP profile, tools/dsp.mjs): the stack figures end before it
    const base = [12, 16].find((b) => r.length > b && r.length === b + 1 + 2 * r[b]);
    if ((base ?? r.length) >= 16) Object.assign(p, { bleHeapFree: (r[12] | r[13] << 7) * 4, bleTaskStackFree: (r[14] | r[15] << 7) * 4 });
    if (base) {                   // each block's share of an audio block's time (%), in the block order
      const f = [], n = this.info ? this.info.nblk : 10;
      for (let i = base + 1; i + 1 < r.length; i += 2) f.push(r[i] | r[i + 1] << 7);
      p.prof = f.slice(0, n);
    }
    return p;
  }
  // v42 (protocol 4): USB AUDIO, the pedal as a USB sound card (docs/USB-AUDIO.md). usbAudio() reads the setting and
  // the streams; usbAudio(true / false) sets it first: the pedal then leaves USB for a second and comes back as a
  // device with or without the audio streams, so this connection ends (connect again after about 3 s).
  // -> { on, rc (0 ok, 1 not in this firmware, 2 flash), recording, flowing, playing, sounding, counts: {...} }
  get hasUsbAudio() { return !!(this.info && this.info.proto >= 4); }
  async usbAudio(on) {
    const r = await this.request(C.USB_AUDIO, on === undefined ? [] : [on ? 1 : 0], 3000);
    const n = r[3] || 0, v = [];
    for (let i = 0; i < n && 4 + 4 * i + 3 < r.length; i++) { const k = 4 + 4 * i; v.push(r[k] | r[k + 1] << 7 | r[k + 2] << 14 | r[k + 3] << 21); }
    const names = ["recStarts", "recPackets", "recFrames", "recEmpty", "recFull", "recNoPacket", "recStalls", "recLonger", "recShorter",
      "recFillLow", "recFillHigh", "playStarts", "playPackets", "playFrames", "playEmpty", "playFull", "blockTicks", "recSkipped", "playSkipped", "blockSwaps"];
    return { on: !!r[0], rc: r[1], recording: !!(r[2] & 1), flowing: !!(r[2] & 2), playing: !!(r[2] & 4), sounding: !!(r[2] & 8),
      counts: Object.fromEntries(names.map((k, i) => [k, v[i] ?? 0])) };
  }
  // v48 (protocol 5): the current preset's footswitches in STOMP mode, and the mode. footswitch() reads; footswitch({ left,
  // right, stomp }) sets what is given first. A footswitch: a block index 0..9 (v75: BST is 9), 12 BYPASS, 15 nothing. The assignment
  // is part of the preset (it marks it edited; SAVE keeps it); STOMP goes to the flash with the next save.
  // -> { left, right, stomp, bypass }
  get hasFootswitch() { return !!(this.info && this.info.proto >= 5); }
  async footswitch(set) {
    const v = (x) => (x === undefined ? 127 : x);
    const args = set ? [v(set.left), v(set.right), set.stomp === undefined ? 127 : set.stomp ? 1 : 0] : [];
    if (set && set.gestures) args.push(...set.gestures.map(v));         // v75 (protocol 9): [left double tap, left hold, right double tap, right hold]; 15: nothing, undefined: leave it
    const r = await this.request(C.FOOTSWITCH, args);
    const o = { left: r[0], right: r[1], stomp: !!r[2], bypass: !!r[3] };
    if (r.length >= 8) o.gestures = [r[4], r[5], r[6], r[7]];             // each an action: 0..9 a block on/off, 12 BYPASS, 16 the tuner, 17 next preset, 18 previous preset, 19 STOMP/PATCH, 15 nothing
    return o;
  }
  // v75 (protocol 8): the blocks the pedal's load safeguard switched off. shed() reads them; shed(true) brings them back (they fade in).
  // -> { mask, blocks: [block indices], count (times since power-on) }
  get hasShed() { return !!(this.info && this.info.proto >= 8); }
  // v75 (protocol 9): the global settings, outside the presets. global() reads them; global(v) sets them (in the pedal's memory: commit() writes them with the store).
  // { tuner: the reference A in Hz (415..466), hpf: input high-pass Hz (0 off, 20..140), eq: five bands in dB (-12..12, 0.5 steps), lim: the limiter's ceiling in dB
  //   (0 off, else -0.5..-12), tails (a delay or reverb plays out when switched off),
//   tunerLights: how the button lights show the tuner (0 HOME blinks, 1 needle, 2 strobe), lightsOff: the lights are dark outside the tuner }
  get hasGlobal() { return !!(this.info && this.info.proto >= 9); }
  async global(v) {
    const args = v ? [v.tuner === 440 ? 0 : Math.max(1, Math.min(52, Math.round(v.tuner) - 414)), Math.round(v.hpf / 5), ...v.eq.map((d) => Math.max(0, Math.min(48, Math.round(d * 2) + 24))),
      Math.max(0, Math.min(24, Math.round(-v.lim * 2) || 0)), v.tails ? 1 : 0, 0, (Math.max(0, Math.min(2, v.tunerLights | 0))) | (v.lightsOff ? 4 : 0)] : [];
    const r = await this.request(C.GLOBAL, args);
    return { tuner: r[0] ? 414 + r[0] : 440, hpf: r[1] * 5, eq: Array.from(r.slice(2, 7), (c) => (c - 24) / 2), lim: r[7] ? -r[7] / 2 : 0, tails: !!r[8], tunerLights: r[10] & 3, lightsOff: !!(r[10] & 4) };
  }
  // what the pedal's tuner hears now: { hz (0 none), open (its tuner page is open), ref (the reference A) }
  // tuner(true) opens the pedal's tuner page (its output is muted, as when the player opens it), tuner(false) closes it
  async tuner(open) {
    const r = await this.request(C.TUNER, open === undefined ? [] : [open ? 1 : 0]);
    return { hz: (r[0] | r[1] << 7 | r[2] << 14) / 100, open: !!r[3], ref: r[4] ? 414 + r[4] : 440 };
  }
  async shed(restore) {
    const r = await this.request(C.SHED, restore ? [1] : []);
    const mask = r[0] | r[1] << 7, n = this.info ? this.info.nblk : 10;
    return { mask, blocks: Array.from({ length: n }, (_, b) => b).filter((b) => mask >> b & 1), count: r[2] | r[3] << 7 };
  }
}

// ---- preset files (JSON, blocks and types by name) ----
export function presetToJSON(q, cat, slot) {
  return {
    slot: slot === undefined ? undefined : slot + 1,
    v: 82,                                                       // patch-level volume; suppressor controls
    name: q.name,
    patchVolume: q.patchVolume,
    ...(q.sceneData && { sceneData: [...q.sceneData] }),
    blocks: cat.blocks.map((b, i) => ({ block: b.label, on: !!q.on[i], type: b.types[q.type[i]] ? b.types[q.type[i]].name : q.type[i], params: [...q.p[i]] })),
    order: q.order.map((i) => cat.blocks[i] ? cat.blocks[i].label : i),
    ...(q.valveOn !== undefined && { valve: { on: q.valveOn, drive: q.valveDrive, level: q.valveLevel } }),
  };
}
export function filePresets(presets) { return { format: "openpocket-presets", version: 1, presets }; }

// a preset from its JSON; problems (unknown block or type) are listed, the rest still comes through
export function presetFromJSON(o, cat, base) {
  const q = clonePreset(base), warn = [];
  delete q.sceneData;
  if (Array.isArray(o.sceneData) && o.sceneData.length === 31 + 2 * (6 + q.p.length * q.p[0].length) &&
      o.sceneData.every((v) => Number.isInteger(v) && v >= 0 && v <= 127)) q.sceneData = [...o.sceneData];
  else if (o.sceneData !== undefined) warn.push("scene data could not be read");
  q.name = cleanName(o.name || q.name);
  q.patchVolume = valveValue(o.patchVolume ?? 50);
  if (q.valveOn !== undefined) {
    q.valveOn = !!o.valve?.on;
    q.valveDrive = valveValue(o.valve?.drive ?? VALVE_DEFAULT);
  }
  if (q.valveLevel !== undefined) q.valveLevel = valveValue(o.valve?.level ?? VALVE_LEVEL_UNITY, VALVE_LEVEL_MAX);
  const byLabel = new Map(cat.blocks.map((b, i) => [b.label, i]));
  const MOVED = ["RANGEMASTER", "EP BOOST", "MAXIMIZER", "ENHANCE"];     // v75: SFX types that live in the BST (PRE / BOOST) block now
  for (let ob of Array.isArray(o.blocks) ? o.blocks : []) {
    if (ob.block === "SFX" && MOVED.includes(ob.type) && byLabel.has("BST")) ob = { ...ob, block: "BST" };
    const i = byLabel.get(ob.block);
    if (i === undefined) { warn.push(`unknown block ${ob.block}`); continue; }
    if (q.valveOn !== undefined && ob.block === "CMP" && (ob.type === "VALVE" || ob.type === 4)) {
      q.valveOn = !!ob.on;
      q.valveDrive = valveValue(ob.params?.[0] ?? VALVE_DEFAULT);
      if (q.valveLevel !== undefined) q.valveLevel = VALVE_LEVEL_UNITY;
      q.on[i] = 0;
      q.type[i] = 0;
      continue;
    }
    q.on[i] = ob.on ? 1 : 0;
    let t = typeof ob.type === "number" ? ob.type : cat.blocks[i].types.findIndex((x) => x.name === ob.type);
    if (t < 0 || t >= cat.blocks[i].types.length) { warn.push(`${ob.block}: unknown type ${ob.type}`); t = q.type[i]; }
    q.type[i] = t;
    if (Array.isArray(ob.params)) for (let k = 0; k < q.p[i].length; k++) {
      const v = Number(ob.params[k]);
      if (Number.isFinite(v)) q.p[i][k] = Math.max(0, Math.min(100, Math.round(v)));
    }
  }
  if (Array.isArray(o.order)) {
    const ord = o.order.map((l) => typeof l === "number" ? l : byLabel.get(l));
    if (ord.length === q.order.length && new Set(ord).size === ord.length && ord.every((x) => x !== undefined && x >= 0 && x < q.order.length)) q.order = ord;
    else if (ord.length < q.order.length && new Set(ord).size === ord.length && ord.every((x) => x !== undefined && x >= 0 && x < q.order.length)) {
      // a file from before a block existed (v75: BST): the new blocks come in after GT (BST's place in the default order), else at the end
      for (let m = 0; m < q.order.length; m++) if (!ord.includes(m)) { const g = cat.blocks[m].label === "BST" ? ord.indexOf(byLabel.get("GT")) : -1; ord.splice(g >= 0 ? g + 1 : ord.length, 0, m); }
      q.order = ord;
    } else warn.push("block order not usable: default order kept");
  }
  return { preset: q, warn };
}
export function parsePresetFile(text) {
  const o = JSON.parse(text);
  if (o && o.format === "openpocket-presets" && Array.isArray(o.presets)) return o.presets;
  if (o && Array.isArray(o.blocks)) return [o];                // a bare preset
  throw fail("format", "not an OpenPocket preset file");
}
