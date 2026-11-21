// The DSP load of the analogue pedal emulations and the poly octave ON THE REAL PEDAL, over USB MIDI (editor protocol). Each test loads an UNSAVED working preset (PUT), lets it run, reads PING
// (load, peak, late blocks, each block's share of the block time), and the slot the pedal had is selected again at the end. Nothing is written to the flash.
//   node tools/research/pedal_load.mjs [--seconds 5]
// With no guitar plugged in the engine runs on silence: the Rangemaster's Newton solver needs fewer iterations at rest, so its load here is a LOWER bound (play through it to see the real one).
import { C, Editor } from "../../docs/pxedit.js";
import { parsePing } from "../dsp.mjs";
const seconds = Number((process.argv[process.argv.indexOf("--seconds") + 1]) || 5);
const sleep = (s) => new Promise((r) => setTimeout(r, s * 1000));
const PROF = ["GATE", "COMP", "OCTAVE", "SYNTH", "SFX", "EQ", "MOD", "DELAY", "REVERB"];
const B = { PITCH: 2, SFX: 4 };
const tests = [
  ["all blocks off (control)", (q) => {}],
  ["ENHANCE (Better Box) BETTER 100", (q) => { q.on[B.SFX] = 1; q.type[B.SFX] = 9; q.p[B.SFX] = [100, 0, 0, 0, 0]; }],
  ["RANGEMASTER VOLUME 100 BODY 0", (q) => { q.on[B.SFX] = 1; q.type[B.SFX] = 10; q.p[B.SFX] = [100, 0, 0, 0, 0]; }],
  ["RANGEMASTER VOLUME 100 BODY 100", (q) => { q.on[B.SFX] = 1; q.type[B.SFX] = 10; q.p[B.SFX] = [100, 0, 100, 0, 0]; }],
  ["EP BOOST GAIN 100 FAT", (q) => { q.on[B.SFX] = 1; q.type[B.SFX] = 11; q.p[B.SFX] = [100, 100, 0, 0, 0]; }],
  ["MAXIMIZER 100 / 100", (q) => { q.on[B.SFX] = 1; q.type[B.SFX] = 12; q.p[B.SFX] = [100, 100, 0, 0, 0]; }],
  ["POLY OCTAVE HQ RANGE 40", (q) => { q.on[B.PITCH] = 1; q.type[B.PITCH] = 4; q.p[B.PITCH] = [0, 60, 0, 40, 0]; }],
  ["POLY OCTAVE HQ RANGE 100", (q) => { q.on[B.PITCH] = 1; q.type[B.PITCH] = 4; q.p[B.PITCH] = [0, 60, 0, 100, 0]; }],
];
const midi = await import("@julusian/midi").then((m) => m.default || m);
const node = (await import("../midi-node.mjs")).nodeMidiAccess(midi);
const ed = new Editor(node);
const info = await ed.connect();
if (!info) { console.log("no pedal answers the editor protocol"); node.close(); process.exit(2); }
const was = (await ed.state()).slot;
console.log(`firmware ${info.version || ""}; the pedal was on slot ${was + 1} (selected again at the end)`);
console.log("test                                     load  peak  late  clip   OCTAVE  SFX   (block share %)");
try {
  for (const [name, edit] of tests) {
    const q = { name: "LOADTEST", on: Array(9).fill(0), type: Array(9).fill(0), p: Array.from({ length: 9 }, () => [0, 0, 0, 0, 0]), order: [0, 1, 2, 3, 4, 5, 6, 7, 8] };
    q.on[0] = 1; edit(q);
    await ed.put(q); await sleep(4);
    const a = parsePing(await ed.request(C.PING)); let b = a, mx = a.load;
    for (let t = 0; t < seconds; t += 0.5) { await sleep(0.5); b = parsePing(await ed.request(C.PING)); mx = Math.max(mx, b.load); }
    const w = (x, y) => (y - x + 16384) % 16384;
    console.log(`${name.padEnd(40)} ${String(b.load).padStart(3)}% ${String(b.peak ?? mx).padStart(4)}% ${String(b.xrun === undefined ? "?" : w(a.xrun, b.xrun)).padStart(5)} ${String(b.clip === undefined ? "?" : w(a.clip, b.clip)).padStart(5)}   ${String(b.prof ? b.prof[2] : "?").padStart(5)}  ${String(b.prof ? b.prof[4] : "?").padStart(4)}   bypass ${b.bypass ? "YES" : "no"}`);
  }
} finally { await ed.select(was).catch(() => {}); ed.close(); node.close(); }
