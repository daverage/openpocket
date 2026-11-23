// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket — end-to-end test of the web editor's protocol (docs/pxedit.js) against a simulated pedal (tests/edsim.c)
// that runs the pedal's real editor code (firmware/px_editor.h), preset store (px_store.h) and engine catalogue.
//   cc -O1 -o build/edsim tests/edsim.c engine/px_dsp.c -lm && node tests/test_editor.mjs
import { spawn } from "node:child_process";
import { mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { Editor, presetToJSON, presetFromJSON, filePresets, parsePresetFile, clonePreset } from "../docs/pxedit.js";

const ROOT = fileURLToPath(new URL("..", import.meta.url));
const SIM = process.env.EDSIM || join(ROOT, "build", "edsim");
const TMP = mkdtempSync(join(tmpdir(), "opedit-"));
globalThis.localStorage = { getItem: () => null, setItem: () => {} };
let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(72)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function sim(args) {
  const child = spawn(SIM, args, { stdio: ["pipe", "pipe", "inherit"] });
  const access = { inputs: new Map(), outputs: new Map() };
  const lines = [];
  let port = null, buf = "", readyFn;
  const ready = new Promise((r) => { readyFn = r; });
  child.stdout.on("data", (d) => {
    buf += d.toString();
    let i;
    while ((i = buf.indexOf("\n")) >= 0) {
      const ln = buf.slice(0, i); buf = buf.slice(i + 1);
      if (ln.startsWith("#port ")) {
        const name = ln.slice(6);
        const input = { name, state: "connected", onmidimessage: null, open: async () => {} };
        const output = { name, state: "connected", open: async () => {}, send: (b) => child.stdin.write(Buffer.from(b).toString("hex") + "\n") };
        port = { input, output };
        access.inputs.set(name, input); access.outputs.set(name, output);
        readyFn();
      } else if (ln.startsWith("#")) lines.push(ln);
      else if (port && port.input.onmidimessage && ln) port.input.onmidimessage({ data: Uint8Array.from(Buffer.from(ln, "hex")) });
    }
  });
  const done = new Promise((r) => child.on("exit", r));
  return {
    access, ready, lines,
    pedal: (l) => child.stdin.write(`#pedal ${l}\n`),
    raw: (l) => child.stdin.write(`${l}\n`),                    // a simulator control line, e.g. "#dsp shed 520"
    dump: async () => { lines.length = 0; child.stdin.write("#dump\n"); for (let i = 0; i < 50 && !lines.length; i++) await sleep(10); return lines[0] || ""; },
    end: () => { child.stdin.end(); return done; },
  };
}

// ---- 1. connect, catalogue, current sound ----
const flash1 = join(TMP, "flash1.bin");
let s = sim(["--dump", flash1]);
await s.ready;
let ed = new Editor(s.access);
const info = await ed.connect();
ok(info && info.proto === 10 && info.version === "v39" && info.identity === "PocketX_939", `INFO: ${info && `${info.version} ${info.identity}`}`);
ok(info && info.nblk === 10 && info.npar === 5 && info.nslot === 52 && info.nameMax === 13, "INFO: 10 blocks, 5 parameters, 52 slots, names of 13");
const t0 = Date.now();
const cat = await ed.catalog();
const cached = { blocks: cat.blocks.map((block, i) => i === 1 ? { ...block, types: block.types.slice(0, -1) } : block) };
globalThis.localStorage.getItem = () => JSON.stringify(cached);
const refreshed = await ed.catalog();
globalThis.localStorage.getItem = () => null;
ok(refreshed.blocks[1].types.length === 5 && refreshed.blocks[1].types.at(-1).name === "", "catalogue refreshes a cached build with fewer compressor types");
const ntypes = cat.blocks.reduce((n, b) => n + b.types.length, 0);
ok(cat.blocks.length === 10 && cat.blocks.map((b) => b.label).join() === "GT,CMP,OCT,SYN,SFX,EQ,MOD,DLY,REV,BST", `catalogue: 10 blocks, ${ntypes} types (${Date.now() - t0} ms)`);
ok(cat.blocks[3].name === "SYNTH" && cat.blocks[3].color === "#ff6500", `catalogue: the SYNTH block and its tile colour (${cat.blocks[3].color})`);
const withTexts = cat.blocks.flatMap((b) => b.types.flatMap((t) => t.params.filter((p) => p.texts))).length;
ok(withTexts > 0 && cat.blocks[7].types[0].params.some((p) => p.texts && p.texts.some((x) => /ms|\d/.test(x))), `catalogue: value texts for ${withTexts} parameters (delay time...)`);
let st = await ed.state();
ok(st.slot === 0 && !st.dirty && st.preset.name.length > 0, `STATE: slot 1 "${st.preset.name}", not changed`);

// v48 (protocol 5): FOOTSWITCH, the current preset's footswitches in STOMP mode
{
  const f0 = await ed.footswitch();
  ok(ed.hasFootswitch && f0.left === 15 && f0.right === 15 && !f0.stomp && !f0.bypass, "FOOTSWITCH: nothing assigned, PATCH mode");
  const f1 = await ed.footswitch({ left: 4, right: 12, stomp: true });
  ok(f1.left === 4 && f1.right === 12 && f1.stomp && (await ed.state()).dirty, "FOOTSWITCH set: left SFX, right BYPASS, STOMP mode; the preset is edited");
  const f2 = await ed.footswitch({ left: 99, stomp: false });
  ok(f2.left === 15 && f2.right === 12 && !f2.stomp, "an out-of-range block is nothing; a field left out stays; PATCH again");
  const fb = await ed.footswitch({ left: 9, right: 10 });
  ok(fb.left === 9 && fb.right === 10, "BST and Valve can each be assigned");
  const gz = await ed.footswitch({ gestures: [17, 16, 5, 19] });
  ok(gz.gestures && gz.gestures.join() === "17,16,5,19" && (await ed.state()).dirty, "gestures: left double tap = next preset, left hold = the tuner, right double tap = block 5, right hold = STOMP/PATCH; the preset is edited");
  const gz2 = await ed.footswitch({ gestures: [undefined, 99, undefined, 15] });
  ok(gz2.gestures.join() === "17,15,5,15", "gestures: one left alone, an unknown action is nothing, 15 clears one");
  await ed.footswitch({ gestures: [15, 15, 15, 15] });
  await ed.footswitch({ left: 15 });
  const f3 = await ed.footswitch({ right: 15 });
  ok(f3.right === 15 && !(await ed.state()).dirty, "nothing assigned again: the preset is as saved");
}

// v75: docs/pxcost.js, the estimate behind the editor's "may not fit" warning, has every live type of every block but the gate
{
  const { COST, costPct } = await import("../docs/pxcost.js");
  const missing = [];
  for (const B of cat.blocks) if (B.label !== "GT") for (const t of B.types) if (t.name && !(`${B.label}:${t.name}` in COST)) missing.push(`${B.label}:${t.name}`);
  ok(missing.length === 0, `pxcost.js covers every live type (missing: ${missing.join(", ") || "none"})`);
  ok(costPct("SYN", "POLY OCT FUZZ") > 40 && costPct("SYN", "POLY OCT FUZZ") > costPct("MOD", "CE CHORUS") * 3 && costPct("GT", "NOISE GATE") === 0, "pxcost.js: POLY OCT FUZZ is the heavy one, a chorus light, the gate unlisted");
}
// v75: the factory bank through the editor: 52 presets, none with a block the pedal could not carry (the estimated processor share of what is on, docs/pxcost.js, under 80 %)
{
  const { COST } = await import("../docs/pxcost.js");
  let worst = 0, worstName = "", bad = [];
  const names = new Set();
  for (let i = 0; i < 52; i++) {
    const g = await ed.slotGet(i), pr = g.preset;
    names.add(pr.name);
    let share = 0;
    pr.on.forEach((on, b) => { if (on && cat.blocks[b].label !== "GT") share += COST[`${cat.blocks[b].label}:${cat.blocks[b].types[pr.type[b]].name}`] || 0; });
    if (share > worst) { worst = share; worstName = pr.name; }
    if (share >= 80) bad.push(`${pr.name} ${share.toFixed(0)}`);
  }
  ok(names.size === 52 && bad.length === 0, `factory bank: 52 different names, every preset under 80 % of the processor (worst ${worstName} ${worst.toFixed(0)} %${bad.length ? "; over: " + bad.join(", ") : ""})`);
}
// v75 (protocol 8): SHED, the blocks the pedal's load safeguard switched off (PING flag bit 2, then SHED says which)
{
  const p0 = await ed.ping();
  ok(ed.hasShed && !p0.shed, "SHED: nothing switched off, PING's flag clear");
  const r0 = await ed.shed();
  ok(r0.mask === 0 && r0.blocks.length === 0 && r0.count === 0, "SHED: no blocks, none since power-on");
  s.raw("#dsp shed 520"); await sleep(80);                  // blocks 3 (SYN) and 9 (BST)
  const p1 = await ed.ping();
  ok(p1.shed && !p1.bypass, "SHED: the safeguard acted: PING's flag bit 2 set, not the bypass");
  const r1 = await ed.shed();
  ok(r1.mask === 520 && r1.blocks.join() === "3,9" && r1.count === 1, `SHED: the blocks are SYN and BST (mask ${r1.mask}, blocks ${r1.blocks})`);
  const r2 = await ed.shed(true);
  ok(r2.mask === 0 && !(await ed.ping()).shed, "SHED(restore): the blocks come back, the flag clears");
}

// ---- 2. live editing ----
const amp = 3;                                  // (v70: the SYNTH block)
const comp = 1, valve = 10;
ok(ed.hasValve && !st.preset.valveOn && st.preset.valveDrive === 71, "Valve defaults to off at +12 dB");
const v1 = await ed.setParam(amp, 0, 77);
st = await ed.state();
ok(v1 === 77 && st.preset.p[amp][0] === 77 && st.dirty, "SET: SYNTH's 1st knob = 77, the sound is now changed (*)");
ok(await ed.setParam(amp, 1, 120) === 100, "SET: a value above 100 is clamped to 100");
ok(await ed.setType(amp, 99) === cat.blocks[amp].types.length - 1, "SET: a type out of range is clamped to the last one");
await ed.setOn(7, 1);
ok((await ed.state()).preset.on[7] === 1, "SET: the delay switched on");
ok(await ed.name("my toneé longer than thirteen") === "my tone? long", "NAME: 13 characters at most, other characters become ?");
const ord = [...st.preset.order]; [ord[0], ord[8]] = [ord[8], ord[0]];
ok(await ed.order(ord) === 0 && (await ed.state()).preset.order.join() === ord.join(), "ORDER: the block order changed");
ok(await ed.order([0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]) === 1, "ORDER: not a permutation -> refused");
const bad = clonePreset(st.preset); bad.p[0][0] = 100; bad.on[3] = 1; bad.type[4] = 60;
ok(await ed.put(bad) === 1, "PUT: a type that does not exist -> refused");
await ed.focus(7);
ok((await ed.state()).focus === 7, "FOCUS: the pedal shows the delay page");

// ---- 3. pushes: a change on the pedal reaches the editor, the editor's own does not come back ----
const pushes = [];
ed.onState = (x) => pushes.push(x);
await ed.watch(true);
s.pedal(`set ${amp} 2 33`);
for (let i = 0; i < 40 && !pushes.length; i++) await sleep(10);
ok(pushes.length === 1 && pushes[0].preset.p[amp][2] === 33, "WATCH: a knob turned on the pedal is pushed to the editor");
pushes.length = 0;
await ed.setParam(amp, 3, 12);
await sleep(150);
ok(pushes.length === 0, "WATCH: the editor's own change is not echoed back");
s.pedal("preset 7");
for (let i = 0; i < 40 && !pushes.length; i++) await sleep(10);
ok(pushes.length >= 1 && pushes.at(-1).slot === 7, "WATCH: a footswitch (slot 8) is pushed");

// ---- 4. saving, slots ----
await ed.select(2);
await ed.setParam(amp, 0, 11);
await ed.name("SAVED THREE");
ok(await ed.save(4) === 0, "SAVE: the current sound into slot 5");
st = await ed.state();
let list = await ed.list();
ok(st.slot === 4 && !st.dirty && list[4].saved && list[4].name === "SAVED THREE", "SAVE: slot 5 is current, saved, named");
ok(list.length === 52 && list.filter((x) => x.saved).length === 1, "LIST: 52 slots, 1 saved");
const g = await ed.slotGet(10);
ok(!g.saved && g.preset.name === list[10].name, `SLOT_GET: slot 11, factory "${g.preset.name}"`);
const a = clonePreset(g.preset); a.name = "STAGED A";
await ed.setType(comp, 1);
await ed.setOn(comp, 1);
await ed.setOn(valve, 1);
await ed.setParam(valve, 0, 75);
const valvePreset = (await ed.state()).preset;
ok(valvePreset.valveOn && valvePreset.valveDrive === 75 && valvePreset.type[comp] === 1 && valvePreset.on[comp] === 1,
  "Valve and Studio compression run independently in the same preset");
a.valveOn = valvePreset.valveOn; a.valveDrive = valvePreset.valveDrive;
a.type[comp] = valvePreset.type[comp]; a.on[comp] = valvePreset.on[comp]; a.p[comp] = valvePreset.p[comp];
const b = clonePreset(g.preset); b.name = "STAGED B"; b.p[amp][0] = 99;
ok(await ed.slotPut(20, a, false) === 0 && await ed.slotPut(21, b, false) === 0, "SLOT_PUT: two slots staged in memory");
ok(await ed.commit() === 0, "COMMIT: one flash write");
ok(await ed.slotPut(4, null, true) === 0 && !(await ed.list())[4].saved, "SLOT_PUT: slot 5 back to its factory preset, written at once");
const dumpLine = await s.dump();
ok(dumpLine.split("|")[2] === "1", "the flash was written only inside the preset store");

// ---- 5. files ----
const q = (await ed.slotGet(20)).preset;
const file = JSON.stringify(filePresets([presetToJSON(q, cat, 20)]));
const back = parsePresetFile(file);
const r = presetFromJSON(back[0], cat, (await ed.slotGet(0)).preset);
ok(r.warn.length === 0 && JSON.stringify(r.preset) === JSON.stringify(q), "file: a preset survives export -> import exactly");
const odd = presetFromJSON({ name: "X", blocks: [{ block: "SYN", on: true, type: "NO SUCH SYNTH", params: [1, 2, 3] }, { block: "AMP" }] }, cat, q);
ok(odd.warn.length === 2 && odd.preset.p[amp][0] === 1, "file: unknown type and block are reported, the rest comes through");
const legacyValve = presetFromJSON({ blocks: [{ block: "CMP", on: true, type: "VALVE", params: [96] }] }, cat, q);
ok(legacyValve.preset.valveOn && legacyValve.preset.valveDrive === 96 && !legacyValve.preset.on[comp], "imports migrate the old CMP VALVE");
// v75 (protocol 9): GLOBAL (the settings outside the presets) and TUNER
{
  const g0 = await ed.global();
  ok(ed.hasGlobal && g0.tuner === 440 && g0.hpf === 0 && g0.eq.every((d) => d === 0) && g0.lim === 0 && g0.tails === true && g0.hush === 0 && g0.tunerLights === 1 && g0.lightsOff === false, "GLOBAL: the defaults (flat, no limiter, tails on, the tuner's needle (v76), lights on)");
  const g1 = await ed.global({ tuner: 432, hpf: 80, eq: [3, -1.5, 0, 6, -12], lim: -3, tails: false, hush: 40, tunerLights: 2, lightsOff: true });
  ok(g1.tuner === 432 && g1.hpf === 80 && g1.eq.join() === "3,-1.5,0,6,-12" && g1.lim === -3 && g1.tails === false && g1.hush === 40 && g1.tunerLights === 2 && g1.lightsOff === true, `GLOBAL set: read back as set (${JSON.stringify(g1)})`);
  const g2 = await ed.global({ tuner: 440, hpf: 0, eq: [50, -50, 0, 0, 0], lim: -99, tails: true, hush: 500 });
  ok(g2.tuner === 440 && g2.eq[0] === 12 && g2.eq[1] === -12 && g2.lim === -12 && g2.hush === 100, "GLOBAL set: out-of-range values are clamped");
  await ed.global({ tuner: 466, hpf: 25, eq: [0, 0, 1, 0, 0], lim: 0, tails: true, hush: 25, tunerLights: 1, lightsOff: false });
  const tu = await ed.tuner();
  ok(tu.hz === 0 && tu.open === false && tu.ref === 466, `TUNER: no note, the page closed, the reference A (${JSON.stringify(tu)})`);
  s.raw("#hz 110.25"); await sleep(80);
  const tu2 = await ed.tuner();
  ok(Math.abs(tu2.hz - 110.25) < 0.011, `TUNER: the frequency in 0.01 Hz (${tu2.hz})`);
  s.raw("#hz 0");
  const to = await ed.tuner(true);
  ok(to.open === true && (await ed.tuner()).open === true, "TUNER(true): the pedal's tuner page opens");
  const tc = await ed.tuner(false);
  ok(tc.open === false && (await ed.tuner()).open === false, "TUNER(false): and closes");
  ok(await ed.commit() === 0, "GLOBAL: written with the store");
}
// ---- v42: USB AUDIO (protocol 4): off until asked for; kept in the store ----
{
  const u0 = await ed.usbAudio();
  const u1 = await ed.usbAudio(true);
  ok(ed.hasUsbAudio && !u0.on && u0.rc === 0 && u1.on && u1.rc === 0, "USB audio: off by default; switched on");
}
await s.end();

// ---- 6. after a power cycle: the store is read back from the flash ----
s = sim([flash1]);
await s.ready;
ed = new Editor(s.access);
await ed.connect();
list = await ed.list();
{
  let u = await ed.usbAudio();
  ok(u.on && u.rc === 0, "USB audio: still on after a power cycle");
  const gp = await ed.global();
  ok(gp.tuner === 466 && gp.hpf === 25 && gp.eq[2] === 1 && gp.hush === 25 && gp.tails === true && gp.tunerLights === 1, `GLOBAL: still set after a power cycle (${JSON.stringify(gp)})`);
  ok(!u.recording && !u.playing && Object.keys(u.counts).length === 20, "USB audio: the streams' state and 20 counts");
  u = await ed.usbAudio(false);
  ok(!u.on && u.rc === 0 && !(await ed.usbAudio()).on, "USB audio: switched off again");
}

ok(list[20].name === "STAGED A" && list[21].name === "STAGED B" && list[20].saved && !list[4].saved, "power cycle: the saved slots are still there");
const savedValve = (await ed.slotGet(20)).preset;
ok(savedValve.valveOn && savedValve.valveDrive === 75 && savedValve.on[comp] && savedValve.type[comp] === 1, "Valve and COMP survive a power cycle");
st = await ed.state();
ok(st.slot === 4, "power cycle: the pedal starts on the last slot SAVEd (imports do not move it)");
await s.end();

console.log(failed ? `\n${failed} FAILED` : "\nall editor tests passed");
process.exit(failed ? 1 : 0);
