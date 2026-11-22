// ENHANCE through the simulator's engine (tools/sim/px_engine.wasm, the code the pedal runs) on the bundled guitar loops at the simulator's pedal level (x0.15).
//   node tests/archive/enhance/enhance_sim.mjs OUTDIR        writes OUTDIR/<loop>__<setting>.wav; measure with: python3 tests/archive/enhance/enhance_sim_measure.py OUTDIR
// Only the SFX block is on (ENHANCE), everything else off, preset level 50 = 0 dB, master 100.
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { GUITAR_LEVEL, PxEngine, presetBytes } from "../../../tools/sim/px-engine.js";
import { engineModule, readWav, run, wavBytes } from "../../../tools/sim/render.mjs";
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..", "..");
const out = process.argv[2]; if (!out) { console.error("usage: node tests/archive/enhance/enhance_sim.mjs OUTDIR"); process.exit(2); }
mkdirSync(out, { recursive: true });
const SFX = 4, ENHANCE = 9;                                  // block index of SFX; PX_SFX_ENHANCE
const settings = { off: null, better35: [35, 0], better70: [70, 0], better100: [100, 0], better100_louder3: [100, 3] };
const module = engineModule();
const { loops } = JSON.parse(readFileSync(join(ROOT, "assets/loops/manifest.json")));
for (const loop of loops) {
  const { samples } = readWav(readFileSync(join(ROOT, "assets/loops", loop.file)));
  const input = samples.map((v) => v * GUITAR_LEVEL);
  for (const [name, par] of Object.entries(settings)) {
    const on = Array(9).fill(0), type = Array(9).fill(0), p = Array.from({ length: 9 }, () => [0, 0, 0, 0, 0]);
    p[0][2] = 50;                                          // the preset level: 0 dB
    if (par) { on[SFX] = 1; type[SFX] = ENHANCE; p[SFX] = [...par, 0, 0, 0]; }
    const engine = new PxEngine(module); engine.factory(0); engine.volume(100);
    engine.preset(presetBytes({ name: "ENH TEST", on, type, p, order: [0, 1, 2, 3, 4, 5, 6, 7, 8] }));
    writeFileSync(join(out, `${loop.file.replace(".wav", "")}__${name}.wav`), wavBytes(run(engine, input)));
  }
  console.log(loop.file, "rendered", Object.keys(settings).length, "settings");
}
