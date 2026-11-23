// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket editor: the global settings (v75, protocol 9): what is set once for the pedal and the room, outside the presets.
// A popover from the header: the tuner's reference A, the input high-pass, the output EQ, the limiter, the delay and reverb tails, the smart gate's hush.
// Changes go to the pedal at once (it plays them), and are written to its flash a moment after the last one (COMMIT: the presets are written with them).
import { app, $, h, on, toast } from "./edcore.js";

const HPF = [0, 20, 25, 30, 40, 55, 70, 80, 100, 120, 140];
const LIM = [0, -0.5, -1, -2, -3, -4, -6, -9, -12];
const BANDS = ["80 Hz", "250 Hz", "1 kHz", "3.5 kHz", "9 kHz"];
const DEFAULTS = { tuner: 440, hpf: 0, eq: [0, 0, 0, 0, 0], lim: 0, tails: true, hush: 0, tunerLights: 1, lightsOff: false };
const TLIGHTS = [0, 1, 2], TL_NAME = ["Blink", "Needle", "Strobe"];
let v = null, sendTimer = 0, saveTimer = 0, saved = true, busy = false;

async function load() {
  if (!app.ed || !app.ed.hasGlobal) { v = null; return; }
  try { v = await app.ed.global(); } catch (_) { v = null; }
}
async function send() {
  if (!v || !app.ed) return;
  try { v = await app.ed.global(v); } catch (_) { return; }
  saved = false; render();
  clearTimeout(saveTimer);
  saveTimer = setTimeout(async () => {                       // a moment after the last change: written with the store
    try { await app.ed.commit(); saved = true; render(); } catch (_) { toast("The pedal could not write the settings.", { kind: "err" }); }
  }, 1500);
}
function change(patch) {
  v = { ...v, ...patch };
  clearTimeout(sendTimer);
  sendTimer = setTimeout(send, 120);
}
// a slider with its value written beside it; --p is the fill the editor's slider style reads
function slider(min, max, step, val, label, fmt, set) {
  const out = h("output", { text: fmt(val) });
  const inp = h("input", { type: "range", min, max, step, value: val, "aria-label": label });
  const paint = () => inp.style.setProperty("--p", `${((+inp.value - min) / (max - min)) * 100}%`);
  paint();
  inp.addEventListener("input", () => { paint(); out.textContent = fmt(+inp.value); set(+inp.value); });
  return h("div", { class: "gl-range" }, inp, out);
}
const sel = (label, opts, cur, fmt, set, help) => h("label", { class: "gl-row" }, h("span", { class: "gl-l", text: label }),
  h("select", { onchange: (e) => set(opts[+e.target.value]) }, opts.map((o, i) => h("option", { value: i, text: fmt(o), selected: o === cur }))),
  help ? h("small", { class: "dim", text: help }) : "");
function render() {
  const pop = $("globpop");
  if (!pop) return;
  pop.textContent = "";
  if (!v) { pop.append(h("h2", { text: "Global settings" }), h("p", { class: "dim", text: app.ed && !app.ed.hasGlobal ? "This firmware has no global settings: update it (OpenPocket v75 or newer)." : "Connect the pedal first." })); return; }
  const refs = Array.from({ length: 52 }, (_, i) => 415 + i);
  pop.append(
    h("h2", { text: "Global settings" }),
    h("p", { class: "dim", text: "Applies across presets. Changes take effect live and save automatically." }),
    ...(app.ed?.hasValve ? [h("p", { class: "dim", text: "On the pedal, hold HOME for the tuner, release it, then hold HOME again for Global." })] : []),
    h("h3", { text: "Input" }),
    sel("High-pass", HPF, v.hpf, (x) => (x ? `${x} Hz` : "Off"), (x) => change({ hpf: x }), "Cuts a steady low rumble before every effect. 25 Hz and below for bass; 80 Hz for a guitar on a loud stage."),
    h("h3", { text: "Output EQ" }),
    h("div", { class: "gl-eq" }, BANDS.map((b, i) => h("label", { class: "gl-band" }, h("span", { text: b }),
      slider(-12, 12, 0.5, v.eq[i], `${b} gain`, (x) => `${x > 0 ? "+" : ""}${x} dB`, (x) => { const eq = [...v.eq]; eq[i] = x; change({ eq }); })))),
    sel("Limiter", LIM, v.lim, (x) => (x ? `${x} dB` : "Off"), (x) => change({ lim: x }), "A ceiling on the output: a loud signal stays under it."),
    h("h3", { text: "Effects" }),
    h("label", { class: "gl-row" }, h("span", { class: "gl-l", text: "Tails" }), h("input", { type: "checkbox", checked: v.tails, onchange: (e) => change({ tails: e.target.checked }) }),
      h("small", { class: "dim", text: "A delay or reverb that is switched off, or left by a preset change, plays out instead of stopping." })),
    h("label", { class: "gl-row" }, h("span", { class: "gl-l", text: "Smart gate hush" }), slider(0, 100, 1, v.hush, "Smart gate hush", (x) => `${x} %`, (x) => change({ hush: x })),
      h("small", { class: "dim", text: "The gate's hiss filter: a low-pass that closes as a note dies away. Only with the SMART GATE." })),
    h("h3", { text: "Tuner" }),
    sel("Reference A", refs, v.tuner, (x) => `${x} Hz`, (x) => change({ tuner: x }), "Turn PARA on the pedal’s tuner page to adjust this on the pedal."),
    sel("Tuner lights", TLIGHTS, v.tunerLights, (x) => TL_NAME[x], (x) => change({ tunerLights: x }), "Blink: HOME blinks. Needle: one light moves along the row with the cents. Strobe: one light chases toward the side the note is out to; the middle three are steady when in tune."),
    h("label", { class: "gl-row" }, h("span", { class: "gl-l", text: "Lights off" }), h("input", { type: "checkbox", checked: v.lightsOff, onchange: (e) => change({ lightsOff: e.target.checked }) }),
      h("small", { class: "dim", text: "The button lights stay dark while you play (the tuner still shows)." })),
    h("div", { class: "gl-foot" }, h("button", { type: "button", class: "btn small", text: "Back to the defaults", onclick: () => { v = { ...DEFAULTS, eq: [...DEFAULTS.eq] }; render(); clearTimeout(sendTimer); send(); } }),
      h("span", { class: "dim", text: saved ? "Saved on the pedal" : "Writing…" })));
}
export function initGlobal() {
  const pop = $("globpop");
  if (!pop) return;
  pop.addEventListener("beforetoggle", async (e) => { if (e.newState === "open") { await load(); render(); } });
  on("conn", () => { v = null; $("glob").hidden = !(app.conn === "ready" && app.ed && app.ed.hasGlobal); });
}
