// Throwaway A/B harness. Run node tools/research/erb_octave_prototype.mjs [output-directory].
import { spawnSync } from "node:child_process";
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { ENGINE_RATE as rate, GUITAR_LEVEL, PxEngine, presetBytes } from "./px-engine.js";
import { engineModule, readWav, run, wavBytes } from "./render.mjs";

const root = fileURLToPath(new URL("../../", import.meta.url));
const scratch = mkdtempSync(join(tmpdir(), "openpocket-erb-"));
const exe = join(scratch, "erb-octave");
const outputDirectory = process.argv[2] && resolve(process.argv[2]);
const wasm = engineModule();

function pedalOctave(input, model = 1) {
  const engine = new PxEngine(wasm);
  const on = Array(12).fill(0), type = Array(12).fill(0);
  const p = Array.from({ length: 12 }, () => Array(5).fill(0));
  on[2] = 1;
  type[2] = model;
  p[2][2] = 100;
  engine.preset(presetBytes({ name: "Octave up", on, type, p, order: [0, 1, 2, 3, 11, 4, 5, 6, 7, 8, 9, 10] }));
  engine.volume(100);
  return run(engine, input);
}

function erb(input, width, divisor = 1) {
  const result = spawnSync(exe, [String(width), String(divisor)], {
    input: Buffer.from(input.buffer, input.byteOffset, input.byteLength),
    maxBuffer: input.byteLength + 65536,
  });
  if (result.error || result.status !== 0 || result.stdout.length !== input.byteLength) {
    throw result.error ?? new Error(`ERB filter failed: ${result.stderr}`);
  }
  return Float32Array.from({ length: input.length }, (_, i) => result.stdout.readFloatLE(4 * i));
}

function rms(samples) {
  return Math.sqrt(samples.reduce((sum, sample) => sum + sample * sample, 0) / samples.length);
}

// The test frequencies complete whole cycles within this one-second analysis window.
function targetEnergy(samples, frequencies) {
  const tail = samples.subarray(rate, rate * 2);
  let target = 0;
  for (const f of frequencies) {
    let real = 0, imag = 0;
    for (let i = 0; i < tail.length; i++) {
      const phase = 2 * Math.PI * (2 * f) * i / rate;
      real += tail[i] * Math.cos(phase);
      imag += tail[i] * Math.sin(phase);
    }
    target += 2 * (real * real + imag * imag) / tail.length ** 2;
  }
  return (100 * target / rms(tail) ** 2).toFixed(1) + "%";
}

function attackRange(process, frequency) {
  const arrivals = [];
  const window = Math.round(rate / (2 * frequency));
  for (let trial = 0; trial < 12; trial++) {
    const start = 4096 + 173 * trial;
    const input = new Float32Array(start + rate / 2);
    for (let i = start; i < input.length; i++) {
      const t = i - start;
      input[i] = 0.15 * Math.min(1, t / 32) * Math.sin(2 * Math.PI * frequency * t / rate);
    }
    const output = process(input);
    const threshold = rms(output.subarray(start + rate / 4)) ** 2 / 4;
    let energy = 0;
    for (let i = start; i < output.length; i++) {
      energy += output[i] ** 2;
      if (i >= start + window) energy -= output[i - window] ** 2;
      if (i >= start + window - 1 && energy / window >= threshold) {
        // Timestamp the centre of the analysis window.
        arrivals.push(1000 * (i - start - (window - 1) / 2) / rate);
        break;
      }
    }
  }
  if (arrivals.length !== 12) return `detected ${arrivals.length}/12`;
  return `${Math.min(...arrivals).toFixed(1)}–${Math.max(...arrivals).toFixed(1)} ms`;
}

try {
  const cc = spawnSync(process.env.CC || "cc", ["-O3", "-std=c11", join(root, "tools/research/erb_octave_prototype.c"), "-lm", "-o", exe], { stdio: "inherit" });
  if (cc.error || cc.status !== 0) throw cc.error ?? new Error("Could not build ERB prototype");
  const candidates = [
    ["stock", pedalOctave],
    ["pedal-hq", input => pedalOctave(input, 5)],
    ["erb-1", input => erb(input, 1)],
    ["erb-1.5", input => erb(input, 1.5)],
    ["erb-1.5-half", input => erb(input, 1.5, 2)],
    ["erb-2", input => erb(input, 2)],
  ];
  if (process.env.BYOD_UP) candidates.push(["byod-v2", input => {
    const result = spawnSync(process.env.BYOD_UP, [], {
      input: Buffer.from(input.buffer, input.byteOffset, input.byteLength), maxBuffer: 16 * 1024 * 1024,
    });
    if (result.error || result.status !== 0 || result.stdout.length !== input.byteLength)
      throw result.error ?? new Error("BYOD reference failed");
    return Float32Array.from({ length: input.length }, (_, i) => result.stdout.readFloatLE(4 * i));
  }]);
  const material = [[73, 110, 147], [82, 110, 147], [82, 87], [196, 247, 330, 392]];
  const chords = material.map(frequencies => Float32Array.from({ length: rate * 2 }, (_, i) =>
    frequencies.reduce((sum, f) => sum + 0.06 * Math.sin(2 * Math.PI * f * i / rate), 0)));
  console.log("                   Energy at exact octave targets                 50%-RMS wet arrival");
  console.log("Candidate     Drop D chord   Low E chord  Close low dyad   High chord       82 Hz      1,500 Hz");
  for (const [name, process] of candidates) {
    const shares = chords.map((input, i) => {
      const output = process(input);
      if (!output.every(Number.isFinite) || !(rms(output) > 0)) throw new Error(`${name} produced invalid audio`);
      return targetEnergy(output, material[i]);
    });
    const delays = [82, 1500].map(frequency => attackRange(process, frequency));
    console.log(name.padEnd(13) + [...shares, ...delays].map(value => value.padStart(14)).join(""));
  }
  if (outputDirectory) {
    mkdirSync(outputDirectory, { recursive: true });
    for (const clip of ["chug", "riff", "chords", "arpeggio", "lead"]) {
      const { samples } = readWav(readFileSync(join(root, "assets/loops", `${clip}.wav`)));
      const input = samples.map(sample => sample * GUITAR_LEVEL);
      for (const [name, process] of [["dry", input => input], ...candidates]) {
        const output = process(input);
        const peak = output.reduce((p, sample) => Math.max(p, Math.abs(sample)), 0);
        const gain = Math.min(0.95 / peak, 0.08 / rms(output));
        writeFileSync(join(outputDirectory, `${clip}-${name}.wav`), wavBytes(output.map(sample => sample * gain)));
      }
    }
    console.log(`Level-matched comparison WAVs in ${outputDirectory}`);
  }
} finally {
  rmSync(scratch, { recursive: true, force: true });
}
