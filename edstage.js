// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket editor: the stage view (v75). A big second display for the pedalboard: put the phone or a laptop next to the pedal.
// The preset that plays, the blocks that are on (tap one to switch it), the two footswitches with what they do (tap one as on the pedal), the next and
// previous preset, a tuner (the pedal's own: it hears the guitar, the editor draws the note), a screen lock and three looks. Everything here is the pedal's
// own state, read through the editor protocol; nothing is kept in the page except the look.
import { app, $, h, on } from "./edcore.js";
import { setOn, setValveOn } from "./edsound.js";
import { VALVE_ACTION } from "./pxedit.js";
import { stepPreset } from "./edpresets.js";

const KEY = "op-stage-look";
const LOOKS = ["color", "amoled", "mono"], LOOK_NAME = { color: "Color", amoled: "AMOLED", mono: "Mono" };
const NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
let isOpen = false, locked = false, tuning = false, look = "color", fs = null, tick = 0, wake = null, inTune = false;
try { const k = localStorage.getItem(KEY); if (LOOKS.includes(k)) look = k; } catch (_) { }

const blockName = (b) => app.cat.blocks[b];
const typeName = (b) => { const t = app.st.preset.type[b], B = blockName(b); return (B.types[t] && B.types[t].name) || ""; };
const ACT = { [VALVE_ACTION]: "Valve on / off", 12: "BYPASS", 16: "Tuner", 17: "Next preset", 18: "Previous preset", 19: "STOMP / PATCH" };
const actName = (a) => (a === 15 || a === undefined ? "" : a < 10 ? `${blockName(a).label} on / off` : ACT[a] || "");

async function readFs() {
  if (!app.ed || !app.ed.hasFootswitch) { fs = null; return; }
  try { fs = await app.ed.footswitch(); } catch (_) { }
}
// the note and the cents off for a frequency against the reference A (the pedal's own sums: firmware/px_tuner.h tn_note)
function noteOf(hz, ref) {
  if (hz <= 20) return null;
  const st = 12 * Math.log2(hz / ref), r = Math.round(st);
  return { name: NOTES[(((r % 12) + 12) + 9) % 12], cents: (st - r) * 100 };
}
function render() {
  const root = $("stage");
  if (!isOpen || !app.st || !app.cat) return;
  root.dataset.look = look;
  root.classList.toggle("locked", locked);
  const p = app.st.preset, num = String(app.st.slot + 1).padStart(2, "0");
  const tools = h("div", { class: "st-tools" },
    h("button", { type: "button", class: "st-btn", text: "◀", "aria-label": "Previous preset", disabled: locked, onclick: () => stepPreset(-1) }),
    h("button", { type: "button", class: "st-btn", text: "▶", "aria-label": "Next preset", disabled: locked, onclick: () => stepPreset(1) }),
    h("button", { type: "button", class: `st-btn${tuning ? " on" : ""}`, text: "Tuner", disabled: locked, onclick: () => (tuning ? stopTuner() : startTuner()) }),
    h("button", { type: "button", class: "st-btn", text: LOOK_NAME[look], disabled: locked, "aria-label": "Display look", onclick: nextLook }),
    h("button", { type: "button", class: "st-btn", text: locked ? "Unlock (hold)" : "Lock", "aria-label": locked ? "Hold to unlock" : "Lock the screen", onpointerdown: lockDown, onpointerup: lockUp, onpointercancel: lockUp }),
    h("button", { type: "button", class: "st-btn", text: "Close", "aria-label": "Close the stage view", disabled: locked, onclick: closeStage }));
  const chain = h("div", { class: "st-chain", role: "group", "aria-label": "Blocks" }, p.order.map((b) => {
    const on_ = !!p.on[b], B = blockName(b);
    return h("button", { type: "button", class: `st-blk${on_ ? " on" : ""}`, style: `--c:${B.color}`, disabled: locked, "aria-pressed": on_ ? "true" : "false", onclick: () => setOn(b, !p.on[b]) },
      h("b", { text: B.label }), h("span", { text: typeName(b) }), h("i", { text: on_ ? "ON" : "OFF" }));
  }));
  const side = (i, name) => {
    if (!fs) return "";
    const t = i ? fs.right : fs.left, b = t < app.cat.blocks.length ? t : null, g = fs.gestures || [];
    const valve = app.ed?.hasValve && t === VALVE_ACTION;
    const on_ = valve ? !!p.valveOn : b !== null && !!p.on[b];
    const what = valve ? "Valve" : b !== null ? `${blockName(b).label} · ${blockName(b).name}` : t === 12 ? "BYPASS" : "Nothing";
    return h("button", { type: "button", class: `st-fs${on_ ? " on" : ""}${t === 12 && fs.bypass ? " on" : ""}`, disabled: locked || (!valve && b === null), style: b !== null ? `--c:${blockName(b).color}` : "", onclick: () => valve ? setValveOn(!on_) : setOn(b, !on_) },
      h("small", { text: name }), h("b", { text: what }),
      h("span", { text: fs.stomp ? (valve || b !== null ? (on_ ? "ON" : "OFF") : t === 12 ? (fs.bypass ? "BYPASSED" : "") : "") : "PATCH: changes the preset" }),
      h("em", { text: [g[i * 2] !== undefined && actName(g[i * 2]) ? `Double: ${actName(g[i * 2])}` : "", g[i * 2 + 1] !== undefined && actName(g[i * 2 + 1]) ? `Hold: ${actName(g[i * 2 + 1])}` : ""].filter(Boolean).join("  ·  ") }));
  };
  const feet = h("div", { class: "st-feet" }, side(0, "Left footswitch"), side(1, "Right footswitch"));
  const kids = [h("div", { class: "st-top" }, h("div", { class: "st-num", text: num }), h("div", { class: "st-name" }, h("span", { text: p.name || "" }), app.st.dirty ? h("small", { text: "EDITED" }) : ""), app.ping && app.ping.bypass ? h("div", { class: "st-warn", text: "BYPASS" }) : ""),
    tools, chain, feet];
  if (tuning) kids.push(h("div", { class: "st-tuner", id: "sttuner", "aria-live": "off" },
    h("div", { class: "st-note", id: "stnote", text: "–" }), h("div", { class: "st-bar" }, h("i", { class: "st-mid" }), h("b", { class: "st-needle", id: "stneedle" })),
    h("div", { class: "st-cents", id: "stcents", text: "" }), h("small", { class: "st-ref", id: "stref", text: "" })));
  root.textContent = "";
  root.append(...kids);
}
function nextLook() { look = LOOKS[(LOOKS.indexOf(look) + 1) % LOOKS.length]; try { localStorage.setItem(KEY, look); } catch (_) { } render(); }
let lockTimer = 0;
function lockDown() { if (!locked) { locked = true; render(); lockTimer = 0; } else lockTimer = setTimeout(() => { locked = false; render(); }, 1000); }   // a tap locks; holding the padlock for a second unlocks
function lockUp() { clearTimeout(lockTimer); }

async function pollTuner() {
  if (!tuning || !app.ed) return;
  try {
    const t = await app.ed.tuner();
    const n = noteOf(t.hz, t.ref), el = $("stnote");
    if (!el) return;
    if (!n) { el.textContent = "–"; $("stcents").textContent = ""; $("stneedle").style.left = "50%"; $("stneedle").hidden = true; inTune = false; }
    else {
      const c = Math.round(n.cents);
      inTune = inTune ? Math.abs(c) < 7 : Math.abs(c) < 4;
      el.textContent = n.name; el.classList.toggle("ok", inTune);
      $("stcents").textContent = `${c > 0 ? "+" : ""}${c} ct`;
      $("stneedle").hidden = false; $("stneedle").style.left = `${50 + Math.max(-50, Math.min(50, n.cents)) * 0.9}%`; $("stneedle").classList.toggle("ok", inTune);
    }
    $("stref").textContent = `A = ${t.ref} Hz${t.open ? " · the pedal's output is muted" : ""}`;
  } catch (_) { }
}
async function startTuner() { tuning = true; try { await app.ed.tuner(true); } catch (_) { } render(); clearInterval(tick); tick = setInterval(pollTuner, 160); }
async function stopTuner() { tuning = false; clearInterval(tick); try { await app.ed.tuner(false); } catch (_) { } render(); }

async function openStage() {
  if (!app.st) return;
  isOpen = true; locked = false;
  $("stage").hidden = false; document.body.classList.add("staged");
  await readFs();
  render();
  try { wake = await navigator.wakeLock.request("screen"); } catch (_) { wake = null; }       // the screen stays on while it is open, where the browser allows it
  $("stage").focus();
}
async function closeStage() {
  if (tuning) await stopTuner();
  isOpen = false; locked = false;
  $("stage").hidden = true; document.body.classList.remove("staged");
  try { if (wake) await wake.release(); } catch (_) { }
  wake = null;
}
export function initStage() {
  const btn = $("stagebtn");
  if (!btn) return;
  btn.onclick = openStage;
  on("conn", () => { btn.hidden = app.conn !== "ready"; if (isOpen && app.conn !== "ready") { tuning = false; clearInterval(tick); closeStage(); } });
  on("state", async () => { if (isOpen) { await readFs(); render(); } });
  on("ping", () => { if (isOpen && app.ping) render(); });
  addEventListener("keydown", (e) => { if (isOpen && e.key === "Escape" && !locked) closeStage(); });
}
