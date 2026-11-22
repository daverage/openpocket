// Loudness of each analogue pedal emulation against the bypassed signal, through the simulator's engine on the bundled guitar loops (x0.15, the pedal's level).
//   node tests/pedal_levels.mjs OUTDIR     then:  python3 tests/pedal_levels_measure.py OUTDIR
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { GUITAR_LEVEL, PxEngine, presetBytes } from "../tools/sim/px-engine.js";
import { engineModule, readWav, run, wavBytes } from "../tools/sim/render.mjs";
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const out = process.argv[2]; if (!out) { console.error("usage: node tests/pedal_levels.mjs OUTDIR"); process.exit(2); }
mkdirSync(out, { recursive: true });
const SFX = 4, BST = 9, T = { ENHANCE: 4, RANGEMASTER: 0, EPBOOST: 1, MAXIMIZER: 2, VOXWAH: 3 };   // v75: the pedals are BST types (block 9)
const S = { off: null };
for (const v of [30, 50, 70, 90, 100]) for (const b of [0, 50, 100]) S[`rm_v${v}_b${b}`] = [T.RANGEMASTER, v, 0, b];
for (const g of [0, 50, 85, 100]) for (const v of [0, 50, 100]) S[`ep_g${g}_v${v}`] = [T.EPBOOST, g, v];
for (const [l, p] of [[0, 50], [50, 0], [50, 100], [100, 0], [0, 100], [50, 50], [100, 50], [100, 100], [10, 10], [30, 30], [70, 70], [25, 25], [75, 75]]) S[`bbe_l${l}_p${p}`] = [T.MAXIMIZER, l, p];
for (const b of [35, 70, 100]) S[`enh_b${b}`] = [T.ENHANCE, b, 0];
for (const ps of [0, 25, 50, 75, 100]) for (const pk of [0, 50, 100]) S[`vw_p${ps}_k${pk}`] = [T.VOXWAH, ps, pk, 0];
const module = engineModule();
const { loops } = JSON.parse(readFileSync(join(ROOT, "assets/loops/manifest.json")));
for (const loop of loops) {
  const { samples } = readWav(readFileSync(join(ROOT, "assets/loops", loop.file)));
  const input = samples.map((v) => v * GUITAR_LEVEL);
  for (const [name, s] of Object.entries(S)) {
    const on = Array(10).fill(0), type = Array(10).fill(0), p = Array.from({ length: 10 }, () => [0, 0, 0, 0, 0]);
    p[0][2] = 50;
    if (s) { const b = s[4] === "sfx" ? SFX : BST; on[b] = 1; type[b] = s[0]; p[b] = [s[1], s[2], s[3] || 0, 0, 0]; }
    const engine = new PxEngine(module); engine.factory(0); engine.volume(100);
    engine.preset(presetBytes({ name: "LEVEL", on, type, p, order: [0, 9, 1, 2, 3, 4, 5, 6, 7, 8] }));
    writeFileSync(join(out, `${loop.file.replace(".wav", "")}__${name}.wav`), wavBytes(run(engine, input)));
  }
}
console.log("rendered", loops.length, "loops x", Object.keys(S).length, "settings");
