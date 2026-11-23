// SPDX-License-Identifier: GPL-3.0-only
// The pedal's engine as WebAssembly (engine/px_wasm.c, tools/sim/px_engine.wasm), on arrays of 128 samples at 44.1 kHz. One
// class for the AudioWorklet (tools/sim/pedal-worklet.js) and the tests. docs/SIMULATOR.md.
export const ENGINE_RATE = 44100;
export const BLOCK = 128;
// tools/calib_levels.py: a guitar at the pedal's level, which a full-scale DI recording reaches at this gain
export const GUITAR_LEVEL = 0.15;
const NAME = 14, NBLK = 10, NPAR = 5;                    // v75: 10 blocks (v70: 9)

// `preset` is the sound as `GET /api/state` reports it; the bytes are px_preset_t (engine/px_dsp.h)
export function presetBytes(preset) {
  if (preset.on.length === NBLK - 1) {          // a nine-block preset (v70..v74): BST added, off, right after the gate in the order
    preset = { ...preset, on: [...preset.on, 0], type: [...preset.type, 0], p: [...preset.p, [50, 0, 0, 0, 0]],
      order: preset.order.flatMap((b) => (b === 0 ? [0, NBLK - 1] : [b])) };
  }
  const size = NAME + NBLK * 3 + NBLK * NPAR;
  const b = new Uint8Array(size + 2);
  b.set([...preset.name].slice(0, NAME - 1).map((c) => c.charCodeAt(0)));
  b.set(preset.on, NAME);
  b.set(preset.type, NAME + NBLK);
  b.set(preset.p.flat(), NAME + 2 * NBLK);
  b.set(preset.order, NAME + 2 * NBLK + NBLK * NPAR);
  b[size] = preset.valveOn ? 1 : 0;
  b[size + 1] = preset.valveDrive ?? 71;
  if (preset.type[1] === 4) {
    b[size] = preset.on[1] ? 1 : 0;
    b[size + 1] = preset.p[1][0];
    b[NAME + 1] = b[NAME + NBLK + 1] = 0;
    b.fill(50, NAME + NBLK * 2 + NPAR, NAME + NBLK * 2 + NPAR * 2);
  }
  return b;
}

export class PxEngine {
  constructor(module) {
    this.x = new WebAssembly.Instance(module).exports;
    const view = (ptr) => new Float32Array(this.x.memory.buffer, ptr, BLOCK);
    this.input = view(this.x.px_input());
    this.output = view(this.x.px_output());
    this.x.px_factory(0);
    this.x.px_reset();
  }
  factory(index) { this.x.px_factory(index); this.x.px_apply(); }
  preset(bytes) { new Uint8Array(this.x.memory.buffer, this.x.px_preset(), bytes.length).set(bytes); this.x.px_apply(); }
  globals(g) { this.x.px_globals(g.hpf, ...g.eq, g.lim, g.tails, g.hush); }
  volume(master) { this.x.px_volume(master); }
  bypass(on) { this.x.px_bypass(on ? 1 : 0); }
  tunerMute(on) { this.x.px_tuner_mute(on ? 1 : 0); }
  process() { this.x.px_process(); }
}
