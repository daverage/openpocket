// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket — the web editor's own logic, without a browser or a pedal: the Control Change map and the learned
// controls (docs/pxmidi.js) against docs/MIDI.md. Run by tests/run_tests.sh.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { ccParam, ccOn, ccToggle, ccType, ccTarget, toCc, fromCc, typeToCc, typeFromCc, decode, learn, resolve, action, cleanMappings, NBLK, NPAR } from "../docs/pxmidi.js";

const ROOT = fileURLToPath(new URL("..", import.meta.url));
const check = async (what, fn) => { await fn(); console.log(`${what.padEnd(76)} ok`); };

// ---- MIDI ----
await check("the Control Change map is the one docs/MIDI.md prints", () => {
  const md = readFileSync(`${ROOT}docs/MIDI.md`, "utf8");
  const rows = [...md.matchAll(/^\| (GATE|COMP|OCTAVE|SYNTH|SFX|EQ|MOD|DELAY|REVERB|BST \(PRE \/ BOOST\)) \| (\d+) \| (\d+) \| (\d+) \| (\d+) \| (\d+) \| (\d+) \| (\d+) \|$/gm)];
  assert.equal(rows.length, NBLK);
  rows.forEach((r, b) => {
    assert.equal(ccOn(b), +r[2]); assert.equal(ccType(b), +r[3]);
    for (let k = 0; k < NPAR; k++) assert.equal(ccParam(b, k), +r[4 + k]);
  });
  assert.equal(ccToggle(5), 13);
});
await check("every number of the map is unique, and ccTarget is its inverse", () => {
  const seen = new Set([7]);
  for (let b = 0; b < NBLK; b++) {
    for (const [cc, t] of [[ccOn(b), { kind: "on", b }], [ccToggle(b), { kind: "toggle", b }], [ccType(b), { kind: "type", b }],
      ...Array.from({ length: NPAR }, (_, k) => [ccParam(b, k), { kind: "param", b, k }])]) {
      assert.ok(!seen.has(cc), `CC ${cc} twice`); seen.add(cc);
      assert.deepEqual(ccTarget(cc), t);
    }
  }
  assert.deepEqual(ccTarget(7), { kind: "master" });
  for (const free of [0, 6, 32, 38, 64, 98, 101, 120, 127]) assert.equal(ccTarget(free), null);
});
await check("values scale as the pedal's: 0..100 to 0..127 and back exactly", () => {
  for (let v = 0; v <= 100; v++) assert.equal(fromCc(toCc(v)), v);
  assert.equal(toCc(100), 127); assert.equal(fromCc(64), 50);
  for (const n of [1, 4, 9, 11]) for (let t = 0; t < n; t++) assert.equal(typeFromCc(typeToCc(t, n), n), t);
  assert.equal(typeFromCc(127, 9), 8);
});
await check("messages are decoded; SysEx and data bytes are not musical", () => {
  assert.deepEqual(decode([0xB2, 11, 99]), { type: "cc", ch: 3, cc: 11, value: 99 });
  assert.deepEqual(decode([0xC0, 51]), { type: "pc", ch: 1, program: 51 });
  assert.equal(decode([0xF8]).type, "clock");
  assert.equal(decode([0xF0, 0x7D, 0xF7]), null);
  assert.equal(decode([0x40, 0x10]), null);
  assert.equal(decode([]), null);
});
await check("a learned control: one control per target and one target per control, in its scope", () => {
  let m = learn([], { ch: 1, cc: 11 }, { kind: "param", b: 5, k: 1 }, null, "a");
  m = learn(m, { ch: 1, cc: 12 }, { kind: "param", b: 5, k: 1 }, null, "b");           // the target taught again
  assert.deepEqual(m.map((x) => x.id), ["b"]);
  m = learn(m, { ch: 1, cc: 12 }, { kind: "on", b: 9, mode: "toggle" }, null, "c");      // the control taught again
  assert.deepEqual(m.map((x) => x.id), ["c"]);
  m = learn(m, { ch: 1, cc: 12 }, { kind: "param", b: 10, k: 2 }, 4, "d");               // the same control, for preset 05 only
  assert.deepEqual(m.map((x) => x.id), ["c", "d"]);
  assert.deepEqual(resolve(m, { ch: 1, cc: 12 }, 4).map((x) => x.id), ["d"], "the preset's own wins in that preset");
  assert.deepEqual(resolve(m, { ch: 1, cc: 12 }, 3).map((x) => x.id), ["c"]);
  assert.deepEqual(resolve(m, { ch: 2, cc: 12 }, 3), [], "another channel is another control");
});
await check("what a learned control does: settings, switches (flip or follow), types, preset steps", () => {
  const ctx = { on: (b) => b === 9, ntypes: () => 8 };
  assert.deepEqual(action({ tgt: { kind: "param", b: 5, k: 1 } }, 127, ctx), { do: "param", b: 5, k: 1, v: 100 });
  assert.deepEqual(action({ tgt: { kind: "on", b: 9, mode: "toggle" } }, 127, ctx), { do: "on", b: 9, v: 0 });
  assert.equal(action({ tgt: { kind: "on", b: 9, mode: "toggle" } }, 0, ctx), null, "the release of a footswitch does nothing");
  assert.deepEqual(action({ tgt: { kind: "on", b: 9, mode: "follow" } }, 0, ctx), { do: "on", b: 9, v: 0 });
  assert.deepEqual(action({ tgt: { kind: "on", b: 2, mode: "follow" } }, 64, ctx), { do: "on", b: 2, v: 1 });
  assert.deepEqual(action({ tgt: { kind: "type", b: 9 } }, 127, ctx), { do: "type", b: 9, t: 7 });
  const live = { on: () => 0, ntypes: () => 30, live: () => [16, 28, 29] };     // Synth2: the retired types are not counted
  assert.deepEqual(action({ tgt: { kind: "type", b: 3 } }, 0, live), { do: "type", b: 3, t: 16 });
  assert.deepEqual(action({ tgt: { kind: "type", b: 3 } }, 127, live), { do: "type", b: 3, t: 29 });
  assert.deepEqual(action({ tgt: { kind: "preset", step: -1 } }, 100, ctx), { do: "preset", step: -1 });
  assert.equal(action({ tgt: { kind: "preset", step: 1 } }, 10, ctx), null);
});
await check("stored controls that are not well formed are dropped", () => {
  const good = { id: "a", src: { ch: 1, cc: 11 }, tgt: { kind: "param", b: 5, k: 1 }, slot: null };
  const list = [good, { src: { ch: 0, cc: 11 }, tgt: good.tgt, slot: null }, { src: good.src, tgt: { kind: "param", b: 12, k: 0 }, slot: null },
    { src: good.src, tgt: { kind: "on", b: 1 }, slot: null }, { src: good.src, tgt: { kind: "preset", step: 2 }, slot: null }, null, "x"];
  assert.deepEqual(cleanMappings(list), [good]);
  assert.deepEqual(cleanMappings("nonsense"), []);
});


console.log("editor logic test passed");
