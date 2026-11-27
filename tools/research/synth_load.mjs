// The DSP load of the SYNTH block's waveform-following types ON THE REAL PEDAL (copy of pedal_load.mjs's method): an UNSAVED working preset each, PING read, the slot restored. Nothing is written to the flash.
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
  ["POLY SHIFTER +7", (q) => { q.on[B.PITCH] = 1; q.type[B.PITCH] = 6; q.p[B.PITCH] = [79, 50, 100, 50, 0]; }],
  ["POLY SHIFTER -12", (q) => { q.on[B.PITCH] = 1; q.type[B.PITCH] = 6; q.p[B.PITCH] = [0, 50, 100, 50, 0]; }],
  ["DROP TUNE (drop D)", (q) => { q.on[B.PITCH] = 1; q.type[B.PITCH] = 7; q.p[B.PITCH] = [30, 50, 100, 0, 0]; }],
  ["POLY OCTAVE HQ (reference)", (q) => { q.on[B.PITCH] = 1; q.type[B.PITCH] = 4; q.p[B.PITCH] = [0, 60, 0, 100, 0]; }],
  ["BANK ORGAN", (q) => { q.on[3] = 1; q.type[3] = 34; q.p[3] = [50, 50, 50, 50, 50]; }],
  ["STRINGS", (q) => { q.on[3] = 1; q.type[3] = 35; q.p[3] = [50, 50, 50, 50, 50]; }],
  ["CHOIR PAD", (q) => { q.on[3] = 1; q.type[3] = 36; q.p[3] = [50, 50, 50, 50, 50]; }],
  ["BRASS", (q) => { q.on[3] = 1; q.type[3] = 37; q.p[3] = [50, 50, 50, 50, 50]; }],
  ["PIPE ORGAN", (q) => { q.on[3] = 1; q.type[3] = 38; q.p[3] = [50, 50, 50, 50, 50]; }],
  ["SQ BRASS", (q) => { q.on[3] = 1; q.type[3] = 45; q.p[3] = [50, 50, 50, 50, 50]; }],
  ["SQ BASS", (q) => { q.on[3] = 1; q.type[3] = 46; q.p[3] = [50, 50, 50, 50, 50]; }],
  ["SQ FLUTE", (q) => { q.on[3] = 1; q.type[3] = 47; q.p[3] = [50, 50, 50, 50, 50]; }],
];
const midi = await import("@julusian/midi").then((m) => m.default || m);
const node = (await import("../midi-node.mjs")).nodeMidiAccess(midi);
const ed = new Editor(node);
const info = await ed.connect();
if (!info) { console.log("no pedal answers the editor protocol"); node.close(); process.exit(2); }
const was = (await ed.state()).slot;
console.log(`firmware ${info.version || ""}; the pedal was on slot ${was + 1} (selected again at the end)`);
console.log("test                                     load  peak  late  clip   OCTAVE SYNTH  (block share %)");
try {
  for (const [name, edit] of tests) {
    const q = { name: "LOADTEST", on: Array(10).fill(0), type: Array(10).fill(0), p: Array.from({ length: 10 }, () => [0, 0, 0, 0, 0]), order: [0, 9, 1, 2, 3, 4, 5, 6, 7, 8] };
    q.on[0] = 1; edit(q);
    await ed.put(q); await sleep(4);
    const a = parsePing(await ed.request(C.PING)); let b = a, mx = a.load;
    for (let t = 0; t < seconds; t += 0.5) { await sleep(0.5); b = parsePing(await ed.request(C.PING)); mx = Math.max(mx, b.load); }
    const w = (x, y) => (y - x + 16384) % 16384;
    console.log(`${name.padEnd(40)} ${String(b.load).padStart(3)}% ${String(b.peak ?? mx).padStart(4)}% ${String(b.xrun === undefined ? "?" : w(a.xrun, b.xrun)).padStart(5)} ${String(b.clip === undefined ? "?" : w(a.clip, b.clip)).padStart(5)}   ${String(b.prof ? b.prof[2] : "?").padStart(5)}  ${String(b.prof ? b.prof[3] : "?").padStart(4)}   bypass ${b.bypass ? "YES" : "no"}`);
  }
} finally { await ed.select(was).catch(() => {}); ed.close(); node.close(); }
