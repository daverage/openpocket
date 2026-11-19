// v49 diagnosis (Synth2: the SYN types are ANALOG, ARCADE and POLY OCT FUZZ; TYPES=... for others): which SYN type takes the second core down? The current sound (not saved) gets each type in turn on
// its SYN block for SEC seconds while you play; after each, PING says whether the second core still runs. The sound
// the pedal had is put back at the end. Read-only for the flash.  node tools/core2_probe.mjs [SEC]
import { C, Editor } from "../docs/pxedit.js";
import { parsePing } from "./dsp.mjs";
const sec = Number(process.argv[2] || 8);
const solo = ["0", "1"].includes(process.env.SOLO_SMART_GATE);
const soloGate = process.env.SOLO_SMART_GATE === "1";
const midi = await import("@julusian/midi").then((m) => m.default || m);
const access = (await import("./midi-node.mjs")).nodeMidiAccess(midi);
const ed = new Editor(access);
const info = await ed.connect();
if (!info) { access.close(); throw new Error("No OpenPocket MIDI pedal found"); }
console.log(`${info.identity} firmware ${info.version}`);
const cat = await ed.catalog();
const SYN = 3, syn = cat.blocks[SYN];   // (v70: SYNTH is block 3)
const want = (process.env.TYPES || "ANALOG,ARCADE,POLY OCT FUZZ,PITCH:POLY OCTAVE HQ").split(",");   // (PITCH:name: a PITCH type)
const st = await ed.state(), was = st.preset;
const sleep = (s) => new Promise((r) => setTimeout(r, s * 1000));
let p = parsePing(await ed.request(C.PING));
console.log(`start: core 2 ${p.secondCore ? "on" : "OFF"}, load ${p.load}%`);
try {
  for (const name of want) {
    const [full, pv] = name.split("@");                   // (NAME@a/b/c/d/e: its five parameters)
    const pitch = full.startsWith("PITCH:"), blk = pitch ? 2 : SYN, nm = pitch ? full.slice(6) : full;
    const t = cat.blocks[blk].types.findIndex((x) => x && x.name === nm);
    if (t < 0) { console.log(`${name}: not in this firmware`); continue; }
    const q = structuredClone(was);
    if (solo) {
      q.on.fill(0);
      q.order = q.order.map((_, i) => i);
      q.p[0][2] = 50; // Match patch level across the replay passes.
    }
    if (soloGate) {
      const gate = cat.blocks[0].types.findIndex((x) => x?.name === "SMART GATE");
      if (gate < 0) throw new Error("SMART GATE is unavailable");
      q.on[0] = 1; q.type[0] = gate; q.p[0] = [25, 35, 50, 25, 50];
    }
    q.on[blk] = 1; q.type[blk] = t; q.p[blk] = pv ? pv.split("/").map(Number) : pitch ? [50, 100, 100, 0, 0] : [50, 50, 50, 50, 0];
    if (pitch) q.on[SYN] = 0;
    for (const b of [5, 6]) q.on[b] = 0;                   // AMP and CAB off: the whole engine then runs on core 2
    if (await ed.put(q) !== 0) throw new Error("Pedal refused the test preset");
    console.log(`${name}: play now (${sec} s)`);
    const start = parsePing(await ed.request(C.PING));
    let peak = 0, bypass = start.bypass;
    for (let i = 0; i < sec * 2; i++) {
      await sleep(0.5);
      p = parsePing(await ed.request(C.PING));
      peak = Math.max(peak, p.peak ?? p.load);
      bypass ||= p.bypass;
      if (!p.secondCore) break;
    }
    const late = start.xrun == null || p.xrun == null ? null : (p.xrun - start.xrun + 16384) % 16384;
    console.log(JSON.stringify({ block: name, secondCore: p.secondCore, peak, bypass, late,
      pitch: p.prof?.[2], syn: p.prof?.[SYN], gate: p.prof?.[0] }));
    if (!p.secondCore) break;
  }
} finally { await ed.put(was); console.log("your sound is back"); ed.close(); access.close?.(); }
