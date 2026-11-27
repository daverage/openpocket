// Reamp each SYNTH type through the real pedal: an UNSAVED working preset per test (only that block on), a DI played out of the
// audio interface into the pedal and the pedal's output recorded back (build/reamp/playrec.py, python sounddevice). The slot the
// pedal had is selected again at the end; nothing is written to the flash.   node tools/research/synth_reamp.mjs
import { Editor } from "../../docs/pxedit.js";
import { execFileSync } from "node:child_process";
const sleep = (s) => new Promise((r) => setTimeout(r, s * 1000));
const LEAD = "My Audio/fast play di.wav", CHORD = "My Audio/scale + duo + power chords di.wav";
const syn = (t, p) => (q) => { q.on[3] = 1; q.type[3] = t; q.p[3] = p; };
const tests = [
  ["dry_lead", LEAD, () => {}], ["dry_chord", CHORD, () => {}],
  ["sfx_square_ref", LEAD, (q) => { q.on[4] = 1; q.type[4] = 6; q.p[4] = [60, 40, 55, 100, 0]; }],
  ["polyoct_ref", CHORD, (q) => { q.on[2] = 1; q.type[2] = 4; q.p[2] = [50, 50, 0, 100, 0]; }],
  ["sine_pshift_+7", "build/reamp/sine220.wav", (q) => { q.on[2] = 1; q.type[2] = 6; q.p[2] = [79, 50, 100, 50, 0]; }],
  ["sine_pshift_+7_mix50", "build/reamp/sine220.wav", (q) => { q.on[2] = 1; q.type[2] = 6; q.p[2] = [79, 50, 50, 50, 0]; }],
  ["sine_pshift_-12", "build/reamp/sine220.wav", (q) => { q.on[2] = 1; q.type[2] = 6; q.p[2] = [0, 50, 100, 50, 0]; }],
  ["sine_pshift_+7_att0", "build/reamp/sine220.wav", (q) => { q.on[2] = 1; q.type[2] = 6; q.p[2] = [79, 50, 100, 0, 0]; }],
  ["sine_polyoct", "build/reamp/sine220.wav", (q) => { q.on[2] = 1; q.type[2] = 4; q.p[2] = [0, 100, 0, 100, 0]; }],
  ["sine_sqbrass", "build/reamp/sine220.wav", (q) => { q.on[3] = 1; q.type[3] = 45; q.p[3] = [50, 40, 50, 0, 50]; }],
  ["sine_whammy", "build/reamp/sine220.wav", (q) => { q.on[2] = 1; q.type[2] = 1; q.p[2] = [75, 100, 0, 0, 0]; }],
  ["sine_dry", "build/reamp/sine220.wav", () => {}],
  ["pshift_+7", CHORD, (q) => { q.on[2] = 1; q.type[2] = 6; q.p[2] = [79, 50, 100, 50, 0]; }],
  ["pshift_-12", CHORD, (q) => { q.on[2] = 1; q.type[2] = 6; q.p[2] = [0, 50, 100, 50, 0]; }],
  ["34_bank_organ", CHORD, syn(34, [10, 50, 25, 50, 50])],
  ["35_strings", CHORD, syn(35, [50, 35, 50, 50, 50])], ["36_choir_pad", CHORD, syn(36, [50, 35, 50, 50, 50])],
  ["37_brass", CHORD, syn(37, [50, 50, 50, 50, 50])], ["38_pipe_organ", CHORD, syn(38, [40, 30, 50, 50, 50])],
  ["39_12_string", CHORD, syn(39, [50, 50, 50, 50, 50])], ["40_bell_pad", CHORD, syn(40, [50, 50, 50, 40, 50])],
  ["41_saw_lead", LEAD, syn(41, [50, 35, 50, 0, 50])], ["42_pulse_lead", LEAD, syn(42, [50, 35, 50, 0, 50])],
  ["43_sub_bass", LEAD, syn(43, [30, 30, 50, 0, 50])], ["44_analog_lead", LEAD, syn(44, [50, 35, 50, 0, 50])],
  ["45_sq_brass", LEAD, syn(45, [50, 40, 50, 0, 50])], ["46_sq_bass", LEAD, syn(46, [30, 30, 50, 30, 50])],
  ["47_sq_flute", LEAD, syn(47, [45, 20, 30, 0, 50])],
];
const midi = await import("@julusian/midi").then((m) => m.default || m);
const node = (await import("../midi-node.mjs")).nodeMidiAccess(midi);
const ed = new Editor(node);
if (!(await ed.connect())) { console.log("no pedal"); process.exit(2); }
const was = (await ed.state()).slot;
try {
  const only = process.argv.slice(2);   // optional: the test names to run
  for (const [name, file, edit] of tests) {
    if (only.length && !only.includes(name)) continue;
    const q = { name: "REAMP", on: Array(10).fill(0), type: Array(10).fill(0), p: Array.from({ length: 10 }, () => [0, 0, 0, 0, 0]), order: [0, 9, 1, 2, 3, 4, 5, 6, 7, 8] };
    edit(q); await ed.put(q); await sleep(1.5);
    execFileSync("python3", ["tools/research/reamp/playrec.py", file, `build/reamp/${name}.wav`]);
    console.log("recorded", name);
  }
} finally { await ed.select(was).catch(() => {}); ed.close(); node.close(); }
