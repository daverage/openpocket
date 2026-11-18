// Host-only comparison. Run node tools/sim/pitch_down_prototype.mjs [output-directory].
import { spawnSync } from "node:child_process";
import { closeSync, mkdtempSync, mkdirSync, openSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { ENGINE_RATE as rate, GUITAR_LEVEL, PxEngine, presetBytes } from "./px-engine.js";
import { engineModule, readWav, run, wavBytes } from "./render.mjs";

const root = fileURLToPath(new URL("../../", import.meta.url));
const outputDirectory = process.argv[2] && resolve(process.argv[2]);
const wasm = engineModule();
const modes = [
  { name: "octave-down", type: 1, controls: [0, 100, 0], ratio: 0.5 },
  // WHAMMY has a 0–100 control over 48 semitones. Value 46 is −1.92 semitones.
  { name: "capo-down", type: 2, controls: [46, 100, 0], ratio: 2 ** (-1.92 / 12) },
];

function stock(input, mode) {
  const engine = new PxEngine(wasm);
  const on = Array(12).fill(0), type = Array(12).fill(0);
  const p = Array.from({ length: 12 }, () => Array(5).fill(0));
  on[2] = 1;
  type[2] = mode.type;
  p[2].splice(0, 3, ...mode.controls);
  p[0][4] = 50;
  engine.preset(presetBytes({ name: "Pitch bench", on, type, p, order: [0, 1, 2, 3, 11, 4, 5, 6, 7, 8, 9, 10] }));
  engine.volume(100);
  return run(engine, input);
}

function spectral(input, mode, window, padding = 1) {
  return runProcessor(process.env.PYTHON || "python3",
    [join(root, "tools/sim/peak_pitch_prototype.py"), String(mode.ratio), String(window), String(padding)], input);
}

function runProcessor(command, args, input) {
  const inputPath = join(scratch, "input.f32"), outputPath = join(scratch, "output.f32");
  writeFileSync(inputPath, Buffer.from(input.buffer, input.byteOffset, input.byteLength));
  const source = openSync(inputPath, "r"), destination = openSync(outputPath, "w");
  try {
    const result = spawnSync(command, args, { stdio: [source, destination, "inherit"], timeout: 30000 });
    if (result.error || result.status !== 0) throw result.error ?? new Error(`Pitch processor exited with ${result.status}`);
  } finally {
    closeSync(source);
    closeSync(destination);
  }
  const output = readFileSync(outputPath);
  if (output.length !== input.byteLength) throw new Error("Pitch processor returned the wrong number of samples");
  return Float32Array.from({ length: input.length }, (_, i) => output.readFloatLE(4 * i));
}

function rms(samples) {
  return Math.sqrt(samples.reduce((sum, sample) => sum + sample ** 2, 0) / samples.length);
}

function targetEnergy(output, frequencies, ratio) {
  // The centre two seconds avoid startup and end effects. A Hann window
  // suppresses leakage for capo targets that do not complete whole cycles.
  const length = 2 * rate;
  const x = output.subarray(rate, rate + length).map((v, i) => v * (0.5 - 0.5 * Math.cos(2 * Math.PI * i / length)));
  const power = rms(x) ** 2;
  let target = 0;
  for (const f of frequencies) {
    let real = 0, imag = 0;
    for (let i = 0; i < length; i++) {
      const phase = 2 * Math.PI * f * ratio * i / rate;
      real += x[i] * Math.cos(phase);
      imag += x[i] * Math.sin(phase);
    }
    // A Hann window has a coherent gain of 1/2 and a mean square of 3/8.
    target += 3 * (real ** 2 + imag ** 2) / length ** 2;
  }
  return `${(100 * target / power).toFixed(1)}%`;
}

function attacks() {
  const starts = Array.from({ length: 12 }, (_, i) => 4096 + i * (rate / 3 + 173));
  const input = new Float32Array(starts.at(-1) + rate / 3);
  for (const start of starts) {
    for (let i = 0; i < rate / 5; ++i) input[start + i] = 0.15 * Math.min(1, i / 32) * Math.sin(2 * Math.PI * 82 * i / rate);
  }
  return { starts, input };
}

function arrivalRange(output, starts, ratio) {
  const window = Math.round(rate / (82 * ratio));
  const arrivals = starts.map(start => {
    const threshold = rms(output.subarray(start + 5600, start + 8000)) ** 2 / 4;
    if (threshold < 1e-10) return NaN;
    let energy = 0;
    for (let i = start; i < start + 5600; ++i) {
      energy += output[i] ** 2;
      if (i >= start + window) energy -= output[i - window] ** 2;
      if (i >= start + window - 1 && energy / window >= threshold) {
        return 1000 * (i - start - (window - 1) / 2) / rate;
      }
    }
    return NaN;
  }).filter(Number.isFinite);
  if (arrivals.length !== starts.length) return `detected ${arrivals.length}/${starts.length}`;
  return `${Math.min(...arrivals).toFixed(1)}–${Math.max(...arrivals).toFixed(1)} ms`;
}

function repickConsistency(mode) {
  const starts = Array.from({ length: 16 }, (_, i) => 4096 + i * 4801);
  const input = new Float32Array(starts.at(-1) + 5500);
  for (const start of starts) {
    for (let i = 0; i < 1600; ++i) {
      const time = i / rate, phase = 2 * Math.PI * 82 * time;
      input[start + i] = 0.13 * Math.min(1, i / 24) * Math.exp(-time / 0.012)
        * (Math.sin(phase) + 0.4 * Math.sin(2 * phase) + 0.2 * Math.sin(3 * phase));
    }
  }
  for (const [name, type] of [["stock", 1], ["pedal-hq", 5]]) {
    const output = stock(input, { ...mode, type });
    const energies = starts.map(start => rms(output.subarray(start, start + 4400)) ** 2);
    const share = 100 * Math.min(...energies) / Math.max(...energies);
    console.log(`${name} weakest repick / strongest repick energy: ${share.toFixed(1)}%`);
  }
}

const material = [[73, 110, 147], [82, 110, 147], [82, 87], [196, 247, 330, 392]];
const chords = material.map(frequencies => Float32Array.from({ length: rate * 4 }, (_, i) =>
  frequencies.reduce((sum, f) => sum + 0.06 * Math.sin(2 * Math.PI * f * i / rate), 0)));
const { starts, input } = attacks();
const scratch = mkdtempSync(join(tmpdir(), "openpocket-pitch-down-"));
process.once("exit", () => rmSync(scratch, { recursive: true, force: true }));
const erb = join(scratch, "erb-octave");
const cc = spawnSync(process.env.CC || "cc", [
  "-O3", "-std=c11", join(root, "tools/sim/erb_octave_prototype.c"), "-lm", "-o", erb,
], { stdio: "inherit" });
if (cc.error || cc.status !== 0) throw cc.error ?? new Error("Could not build ERB prototype");

const spectralCandidates = [
  ["stock", stock],
  ...[512, 1024, 2048, 4096].map(size => [`fft-${size}`, (samples, mode) => spectral(samples, mode, size)]),
  ["fft-512-pad4", (samples, mode) => spectral(samples, mode, 512, 4)],
  ["fft-1024-pad4", (samples, mode) => spectral(samples, mode, 1024, 4)],
];

for (const mode of modes) {
  const candidates = mode.ratio === 0.5
    ? [spectralCandidates[0], ["pedal-hq", (samples, config) => stock(samples, { ...config, type: 5 })],
      ...spectralCandidates.slice(1),
      ["erb-down-1", samples => runProcessor(erb, ["1", "2", "down"], samples)],
      ["erb-down-1.5", samples => runProcessor(erb, ["1.5", "2", "down"], samples)]]
    : spectralCandidates;
  console.log(`\n${mode.name}        Wet energy at target pitches                        Low E attack`);
  console.log("Candidate     Drop D chord   Low E chord  Close low dyad   High chord      50% RMS");
  for (const [name, process] of candidates) {
    const shares = chords.map((samples, i) => {
      const output = process(samples, mode);
      if (!output.every(Number.isFinite) || !(rms(output) > 0)) throw new Error(`${name} produced invalid audio`);
      return targetEnergy(output, material[i], mode.ratio);
    });
    const arrival = arrivalRange(process(input, mode), starts, mode.ratio);
    console.log(name.padEnd(13) + [...shares, arrival].map(value => value.padStart(14)).join(""));
  }
  if (mode.ratio === 0.5) repickConsistency(mode);
  if (!outputDirectory) continue;
  mkdirSync(outputDirectory, { recursive: true });
  for (const clip of ["chug", "riff", "chords"]) {
    const { samples } = readWav(readFileSync(join(root, "assets/loops", `${clip}.wav`)));
    const dry = samples.map(sample => sample * GUITAR_LEVEL);
    for (const [name, process] of candidates) {
      const output = process(dry, mode);
      const peak = output.reduce((p, sample) => Math.max(p, Math.abs(sample)), 0);
      const gain = Math.min(0.95 / peak, 0.08 / rms(output));
      writeFileSync(join(outputDirectory, `${clip}-${mode.name}-${name}.wav`), wavBytes(output.map(sample => sample * gain)));
    }
  }
}
if (outputDirectory) console.log(`\nLevel-matched comparison WAVs in ${outputDirectory}`);
