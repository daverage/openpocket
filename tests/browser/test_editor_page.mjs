// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket — the web editor page (docs/editor.html) in a real browser (Playwright / Chromium) against the simulated
// pedal (tests/edsim.c: the pedal's real editor code and preset store), through a fake Web MIDI access with the pedal
// and a second device (a foot controller). TONE3000 is simulated (its pop-up, its sign-in, its lists and files).
//   node tests/browser/test_editor_page.mjs [OUT_DIR]     (screenshots in OUT_DIR, default build/editor)
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, writeFileSync, mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, extname } from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
const require = createRequire(import.meta.url);
const { chromium } = require("playwright");   /* the global install (CommonJS) */

const ROOT = fileURLToPath(new URL("../..", import.meta.url));
const OUT = process.argv[2] || join(ROOT, "build", "editor");
const TMP = mkdtempSync(join(tmpdir(), "opedpage-"));
mkdirSync(OUT, { recursive: true });
const SITE = join(ROOT, "docs");
const TYPES = { ".html": "text/html", ".js": "text/javascript", ".json": "application/json", ".css": "text/css", ".wav": "audio/wav" };
const server = createServer((req, res) => {
  try {
    const p = join(SITE, decodeURIComponent(req.url.split("?")[0]).replace(/\/$/, "/index.html"));
    res.writeHead(200, { "content-type": TYPES[extname(p)] || "application/octet-stream" });
    res.end(readFileSync(p));
  } catch (_) { res.writeHead(404); res.end(); }
}).listen(0);
const url = `http://127.0.0.1:${server.address().port}/editor.html`;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(78)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };

const browser = await chromium.launch();
const ctx = await browser.newContext({ viewport: { width: 1440, height: 900 }, deviceScaleFactor: 2, acceptDownloads: true });
const page = await ctx.newPage();
let sim = null, buf = "";
const states = [];
const FLASH = join(TMP, "flash.bin");
await page.exposeFunction("simSend", (hex) => sim && sim.stdin.write(hex + "\n"));
page.on("console", (m) => { if (m.type() === "error" && !/fonts\.g|ERR_TUNNEL|net::|Failed to load resource/.test(m.text())) { console.log("page error:", m.text()); failed++; } });
page.on("pageerror", (e) => { console.log("page exception:", e.message); failed++; });
await page.addInitScript(() => {
  const access = { inputs: new Map(), outputs: new Map(), onstatechange: null };
  const fire = (port) => { if (access.onstatechange) access.onstatechange({ port }); };
  const mkIn = (name) => ({ id: "in-" + name, name, type: "input", state: "connected", onmidimessage: null, open: async () => {} });
  let port = null;
  const ctl = mkIn("Foot Controller");                     // another MIDI device on the computer
  access.inputs.set(ctl.id, ctl);
  window.__simPort = (name) => {
    const input = mkIn(name);
    const output = { id: "out-" + name, name, type: "output", state: "connected", open: async () => {}, send: (b) => window.simSend([...b].map((x) => x.toString(16).padStart(2, "0")).join("")) };
    port = { input, output };
    access.inputs.set(input.id, input); access.outputs.set(output.id, output);
    fire(input);
  };
  window.__simGone = () => { if (!port) return; port.input.state = port.output.state = "disconnected"; access.inputs.delete(port.input.id); access.outputs.delete(port.output.id); const p = port.input; port = null; fire(p); };
  // window.__prof = [13 figures]: PING's reply gets the DSP profile a v40.2 pedal adds (the simulator has none)
  window.__simIn = (hex) => {
    let d = hex.match(/../g).map((h) => parseInt(h, 16));
    if (window.__prof && d[4] === 18 && d.length === 18) d = [...d.slice(0, -1), window.__prof.length, ...window.__prof.flatMap((v) => [v & 127, v >> 7]), 0xF7];
    if (port && port.input.onmidimessage) port.input.onmidimessage({ data: Uint8Array.from(d) });
  };
  window.__ctl = (bytes) => { if (ctl.onmidimessage) ctl.onmidimessage({ data: Uint8Array.from(bytes) }); };
  navigator.requestMIDIAccess = async () => access;
});
function startSim() {
  buf = "";
  sim = spawn(join(ROOT, "build", "edsim"), ["--persist", FLASH], { stdio: ["pipe", "pipe", "inherit"] });
  sim.stdout.on("data", (d) => {
    buf += d.toString();
    let i;
    while ((i = buf.indexOf("\n")) >= 0) {
      const ln = buf.slice(0, i); buf = buf.slice(i + 1);
      if (ln.startsWith("#port ")) page.evaluate((n) => window.__simPort(n), ln.slice(6)).catch(() => {});
      else if (ln.startsWith("#state ")) states.push(ln.slice(7).split("|"));
      else if (ln) page.evaluate((h) => window.__simIn(h), ln).catch(() => {});
    }
  });
}
async function pedalState() {
  states.length = 0; sim.stdin.write("#dump\n");
  for (let i = 0; i < 100 && !states.length; i++) await sleep(10);
  const s = states[0];
  return { slot: +s[0], name: s[1], writesOk: s[2] === "1", focus: +s[3], on: s[4].split(",").map(Number), type: s[5].split(",").map(Number), amp: s[6].split(",").map(Number), order: s[7].split(",").map(Number) };
}
const shot = (n) => page.screenshot({ path: join(OUT, n) });
const text = (sel) => page.textContent(sel);
const row = (slot) => page.locator(`#ps${slot}`);
// a message of the page (they leave by themselves: each wait starts from a clean slate)
const clearToasts = () => page.evaluate(() => { document.getElementById("toasts").textContent = ""; });
const toast = (re, ms = 8000) => page.waitForFunction((r) => new RegExp(r).test(document.getElementById("toasts").textContent), re.source, { timeout: ms }).then(() => true, () => false);
const dialog = async () => { await page.waitForSelector("#dlg[open]", { timeout: 4000 }); await sleep(220); };
const menuItem = (label) => page.locator("#menu button", { hasText: label }).first().click();
const go = (view) => page.click(`#tabs [data-go="${view}"]`);
// an option by its text, spaces counted loosely: an option's label has its runs of spaces collapsed, and Playwright
// versions differ in whether { label } compares it so
const pickOpt = async (sel, re) => page.selectOption(sel, await page.$$eval(`${sel} option`, (o, src) => (o.find((x) => new RegExp(src).test(x.textContent)) || {}).value, re.source));
const slide = async (sel, frac) => { const b = await page.locator(sel).boundingBox(); await page.mouse.click(b.x + b.width * frac, b.y + b.height / 2); await sleep(300); };

// ---- first run, no pedal, then the pedal appears ----
await page.goto(url);
await sleep(300);
ok(await page.isVisible("#connect") && /Connect your pedal/.test(await text("#gate")), "first run: what to do, one Connect button");
await shot("editor_1_idle.png");
await page.click("#connect");
await page.waitForSelector('body[data-conn="notfound"]', { timeout: 5000 });
ok(/No pedal answered/.test(await text("#gate")) && /Foot Controller/.test(await text("#seen")), "no pedal: the reasons to check, and the MIDI devices the browser does see");
await shot("editor_1_no_pedal.png");
startSim();
await page.waitForSelector('body[data-conn="ready"]', { timeout: 20000 });
ok(true, "a pedal plugged in afterwards is found without another click");
ok(/PocketX · OpenPocket v39/.test(await text("#status")), `status: ${await text("#status")}`);
ok(await page.locator("#list .row").count() === 52, "52 presets listed");
ok(await page.inputValue("#name") === "CLEAN HALL" && await text("#num") === "01", "header: 01 CLEAN HALL");
ok(await page.locator("#tiles .tile").count() === 10, "10 blocks in the chain");
await page.waitForFunction(() => /42%/.test(document.getElementById("loadv").textContent), null, { timeout: 4000 }).catch(() => {});
ok(/42%/.test(await text("#loadv")) && await page.getAttribute("#load", "data-level") === "ok", "top bar: the DSP load (PING)");
await page.waitForFunction(() => /CMP/.test(document.querySelector("#ps0 .sum").textContent), null, { timeout: 8000 }).catch(() => {});
ok(/CMP REV/.test(await text("#ps0 .sum")), `the list says what each preset is made of: ${await text("#ps0 .sum")}`);
await shot("editor_2_connected.png");
await page.click("#load");
ok(/Room to spare/.test(await text("#loadpop")) && /Peak, this preset/.test(await text("#loadpop")), "DSP load details: the peaks, late blocks, clipping");
await page.keyboard.press("Escape");
// a v40.2 pedal also says where the time goes: each block's share (by block id: GT CMP OCT SYN SFX EQ MOD DLY REV BST)
await page.evaluate(() => { window.__prof = [1, 2, 0, 0, 0, 3, 9, 1, 0, 0]; });
await page.waitForFunction(() => parseFloat(document.querySelector('#tiles .tile[data-b="6"] .share').style.width) > 0, null, { timeout: 4000 }).catch(() => {});
await page.click("#load");
ok(/Where it goes/.test(await text("#loadpop")) && /9 %/.test(await text("#loadpop")) && /MOD/.test(await text("#loadpop .prof")) && !/OCT/.test(await text("#loadpop .prof")),
  "with a v40.2 PING: where the DSP time goes, block by block (and a mark under each block)");
await page.keyboard.press("Escape");
await page.evaluate(() => { window.__prof = null; });

// ---- a setting in the page -> the pedal ----
await page.click('#tiles .tile[data-b="3"] .tsel');
await sleep(150);
ok((await pedalState()).focus === 3, "click on the SYN block: the pedal shows the SYNTH page");
await pickOpt("#type", /BANK ORGAN/);
await sleep(250);
ok((await pedalState()).type[3] === 34, "SYNTH type BANK ORGAN chosen in the page -> the pedal");
ok(await page.locator("#p0").getAttribute("role") === "radiogroup" && await page.locator("#p0 button").count() === 4, "BANK ORGAN's REGISTER is four buttons (JAZZ, FULL, GOSPEL, CHURCH), not a slider");
await page.locator("#p0 button", { hasText: "FULL" }).click();
await sleep(250);
let ps = await pedalState();
ok(ps.amp[0] >= 25 && ps.amp[0] < 50, `REGISTER FULL chosen: the pedal's knob is at ${ps.amp[0]} (25..49)`);
await slide("#p1", 0.8);
ps = await pedalState();
ok(Math.abs(ps.amp[1] - 80) <= 3, `SYNTH slider 2 (PERC) clicked at 80 %: the pedal has ${ps.amp[1]}`);
ok(await text("#star") === "Edited" && await page.isEnabled("#save"), "the header says Edited, Save is ready");
await page.focus("#p2");
await page.keyboard.press("ArrowRight");
await page.keyboard.press("ArrowRight");
await sleep(250);
ps = await pedalState();
ok(ps.amp[2] === +(await page.inputValue("#p2")), `keyboard: slider 3 = ${await page.inputValue("#p2")} in the page, ${ps.amp[2]} on the pedal`);
const before3 = ps.amp[3];
await page.locator(".prm").nth(3).locator(".nudge").nth(1).click();
await sleep(250);
ok((await pedalState()).amp[3] === before3 + 1, "the + beside a slider: one step up");

// ---- the pedal -> the page ----
sim.stdin.write("#pedal set 3 2 7\n");
await page.waitForFunction(() => document.getElementById("p2").value === "7", null, { timeout: 3000 }).catch(() => {});
ok(await page.inputValue("#p2") === "7", "a knob turned on the pedal moves the slider in the page");
sim.stdin.write("#pedal focus 7\n");
await page.waitForFunction(() => /DELAY/.test(document.getElementById("bname").textContent), null, { timeout: 3000 }).catch(() => {});
ok(/DELAY/.test(await text("#bname")), "the DLY button on the pedal opens the delay in the page");

// ---- type, on/off, order ----
await page.selectOption("#type", "3");
await sleep(200);
ok((await pedalState()).type[7] === 3, "type chosen in the page -> the pedal");
await page.click("#tnext");
await sleep(200);
ok((await pedalState()).type[7] === 4, "the > button: next type");
ok(await page.locator("#p3").evaluate((e) => e.tagName) === "SELECT" && await page.locator("#p3 option").count() === 7, "a setting with named values (SPACE ECHO's HEADS: 1, 2, 1+2 ... ALL) is a list of them");
const wasOn = (await pedalState()).on[7];
await page.click("#onoff");
await sleep(200);
ok((await pedalState()).on[7] === (wasOn ? 0 : 1) && await page.getAttribute("#onoff", "aria-checked") === (wasOn ? "false" : "true"), "the block's switch: on/off, and it says so");
const modOn = (await pedalState()).on[6];
await page.click('#tiles .tile[data-b="6"] .pw');
await sleep(200);
ok((await pedalState()).on[6] === (modOn ? 0 : 1) && new RegExp(modOn ? "OFF" : "ON").test(await text('#tiles .tile[data-b="6"] .pw')), `the switch on the MOD block switches it ${modOn ? "off" : "on"} (and reads ${modOn ? "OFF" : "ON"})`);
const before = (await pedalState()).order;
const t0 = await page.locator("#tiles .tile").nth(0).boundingBox();
const t3 = await page.locator("#tiles .tile").nth(3).boundingBox();
await page.mouse.move(t0.x + t0.width / 2, t0.y + 14);
await page.mouse.down();
await page.mouse.move(t0.x + t0.width / 2 + 20, t0.y + 14, { steps: 4 });
await page.mouse.move(t3.x + t3.width / 2, t3.y + 14, { steps: 10 });
await shot("editor_3_drag.png");
await page.mouse.up();
await sleep(300);
const after = (await pedalState()).order;
ok(after[3] === before[0] && after[0] === before[1], `drag GT to the 4th place: the pedal's order ${after.join(",")}`);
await page.click('#tiles .tile[data-b="7"] .tsel');
await page.click("#mvl");
await sleep(250);
const a2 = (await pedalState()).order;
ok(a2.indexOf(7) === after.indexOf(7) - 1, "the earlier button: the delay one place earlier");
// the chain by keyboard: arrows choose, Space switches, Alt + arrows move
await page.focus('#tiles .tile[data-b="7"] .tsel');
await page.keyboard.press("ArrowRight");
await sleep(200);
const nextB = a2[a2.indexOf(7) + 1];
ok((await pedalState()).focus === nextB, "keyboard: the right arrow chooses the next block of the chain");
const onB = (await pedalState()).on[nextB];
await page.keyboard.press("Space");
await sleep(200);
ok((await pedalState()).on[nextB] === (onB ? 0 : 1), "keyboard: Space switches the block");
await page.keyboard.press("Alt+ArrowRight");
await sleep(300);
const a3 = (await pedalState()).order;
ok(a3.indexOf(nextB) === a2.indexOf(nextB) + 1, "keyboard: Alt + right arrow moves the block one place later");
// a setting with two named values is a pair of buttons (the rotary's SPEED)
await page.click('#tiles .tile[data-b="6"] .tsel');
await page.selectOption("#type", { label: "ROTARY" });
await sleep(250);
ok(await page.locator("#p0").getAttribute("role") === "radiogroup", "ROTARY's SPEED (SLOW / FAST) is two buttons");
await page.locator("#p0 button", { hasText: "FAST" }).click();
await sleep(250);
ok(await page.locator("#p0 button", { hasText: "FAST" }).getAttribute("aria-checked") === "true", "FAST chosen");

// ---- name, save, undo, compare ----
await page.fill("#name", "MY BIG TONE");
await page.press("#name", "Enter");
await sleep(200);
ok((await pedalState()).name === "MY BIG TONE", "renamed on the pedal");
await clearToasts();
await page.keyboard.press("Control+s");
ok(await toast(/Saved as 01 MY BIG TONE/), "Ctrl + S saves");
ok(await text("#star") === "" && /Yours/.test(await row(0).textContent()), "saved: no more Edited, 01 is marked Yours");
await shot("editor_4_saved.png");
await page.locator("#toasts .toast-a").click();
ok(await toast(/Undone/), "Undo after the save");
ok(!/Yours/.test(await row(0).textContent()) && await text("#star") === "Edited" && (await pedalState()).name === "MY BIG TONE", "undone: 01 is the factory preset again, and the edited sound is still playing");
await clearToasts();
await page.click("#save");
ok(await toast(/Saved as 01/), "saved again");
await page.click('#tiles .tile[data-b="3"] .tsel');
const savedGain = (await pedalState()).amp[1];
await slide("#p1", 0.2);
const editedGain = (await pedalState()).amp[1];
await page.click("#compare");
await page.waitForFunction(() => /Hearing the saved preset/.test(document.getElementById("star").textContent), null, { timeout: 3000 });
ok((await pedalState()).amp[1] === savedGain && await page.locator("#block").evaluate((e) => e.inert), `Compare: the pedal plays the saved preset (gain ${savedGain}), the editor waits`);
await page.click("#compare");
await sleep(300);
ok((await pedalState()).amp[1] === editedGain && await text("#star") === "Edited", `Compare again: back to the edited sound (gain ${editedGain})`);
await page.click("#revert");
await sleep(300);
ok((await pedalState()).amp[1] === savedGain && await text("#star") === "", "Revert: the saved preset again");

// ---- presets: load, follow the pedal, unsaved changes ----
await row(6).click();
await sleep(300);
ok((await pedalState()).slot === 6 && await text("#num") === "07", "click on 07 in the list: the pedal loads it");
sim.stdin.write("#pedal preset 2\n");
await page.waitForFunction(() => document.getElementById("num").textContent === "03", null, { timeout: 3000 }).catch(() => {});
ok(await text("#num") === "03" && await row(2).getAttribute("aria-selected") === "true", "footswitch on the pedal: the page follows (03)");
await page.click('#tiles .tile[data-b="3"] .tsel');
await slide("#p1", 0.25);
await row(3).click();
await dialog();
ok(/not saved/.test(await text("#dlg")), "leaving an unsaved sound asks first: save, drop or stay");
await shot("editor_5_unsaved.png");
await page.locator("#dlg button", { hasText: "Cancel" }).click();
ok((await pedalState()).slot === 2, "Cancel: still on the same preset");

// ---- save as, with what it replaces said first ----
await page.click("#saveas");
await dialog();
await page.click("#pick-9");
ok(/factory preset ENHANCED/.test(await text(".pick-note")) && /Save as 10/.test(await text("#dlg-ok")), "Save as: choosing 10 says what is there (a factory preset) before anything is written");
await clearToasts();
await page.click("#dlg-ok");
ok(await toast(/Saved as 10/), "Save as 10");
ok((await pedalState()).slot === 9 && /Yours/.test(await row(9).textContent()), "saved in 10, which is now the current preset");
const name10 = await page.inputValue("#name");
await page.click("#saveas");
await dialog();
await page.click("#pick-0");
ok(/your preset MY BIG TONE/.test(await text(".pick-note")) && /Replace 01 MY BIG TONE/.test(await text("#dlg-ok")), "Save as over a preset of yours: the button names what it replaces");
await page.locator("#dlg button", { hasText: "Cancel" }).click();

// ---- move, copy, rename, back to factory ----
await clearToasts();
await row(9).locator(".more").click();
await menuItem("Move up");
ok(await toast(/is preset 09 now/), "Move up");
ok((await pedalState()).slot === 8 && (await row(8).textContent()).includes(name10) && /WAH PARKED HI/.test(await row(9).textContent()), "the preset is 09 now, the one that was there is 10, and the pedal followed");
await clearToasts();
await page.focus("#list");
await page.keyboard.press("Alt+ArrowDown");
ok(await toast(/is preset 10 now/), "keyboard: Alt + down arrow in the list moves the preset back");
ok(/WAH PARKED HI/.test(await row(8).textContent()) && !/Yours/.test(await row(8).textContent()), "a factory preset moved away and back is the factory preset again, not a copy of yours");
await clearToasts();
await row(9).locator(".more").click();
await menuItem("Move to…");
await dialog();
await page.click("#pick-19");
ok(/becomes preset 20/.test(await text(".pick-note")), "Move to…: it says where the preset goes and what moves");
await page.click("#dlg-ok");
ok(await toast(/is preset 20 now/), "moved to 20");
ok((await row(19).textContent()).includes(name10) && /POLY OCTAVES/.test(await row(9).textContent()) && (await pedalState()).slot === 19, "20 holds it, the presets between moved up by one");
await page.locator("#toasts .toast-a").click();
ok(await toast(/Undone: the move/), "Undo of the move");
ok((await row(9).textContent()).includes(name10) && /POLY OCTAVES/.test(await row(10).textContent()) && !/Yours/.test(await row(10).textContent()), "everything is back where it was, the factory presets factory again");
await clearToasts();
await row(9).locator(".more").click();
await menuItem("Copy to…");
await dialog();
await page.click("#pick-40");
await page.click("#dlg-ok");
ok(await toast(/is also preset 41 now/), "Copy to 41");
ok((await row(40).textContent()).includes(name10), "41 holds the copy");
await clearToasts();
await row(40).locator(".more").click();
await menuItem("Rename…");
await dialog();
await page.fill("#dlg-input", "THE COPY");
await page.click("#dlg-ok");
ok(await toast(/Preset 41 is THE COPY now/), "Rename a preset that is not the current one");
await clearToasts();
await row(40).locator(".more").click();
await menuItem("Back to factory");
await dialog();
ok(/THE COPY leaves preset 41/.test(await text("#dlg")), "Back to factory asks, and names the preset that goes");
await page.click("#dlg-ok");
ok(await toast(/factory .* again/), "41 is the factory preset again");
ok(!/Yours/.test(await row(40).textContent()), "41 no longer marked Yours");

// ---- search, filter ----
await page.keyboard.press("/");
ok(await page.evaluate(() => document.activeElement.id === "psearch"), "the / key goes to the search field");
await page.fill("#psearch", "organ");
const found = await page.locator("#list .row").allTextContents();
ok(found.length >= 3 && found.length < 52 && found.every((t) => /ORGAN/.test(t)), `search "organ": ${found.length} presets (JAZZ, PIPE, FULL ORGAN)`);
await page.fill("#psearch", "zzzz");
ok(/No preset matches/.test(await text("#listempty")), "a search with no result says so");
await page.fill("#psearch", "");
await page.click('#pfilter [data-f="user"]');
ok(await page.locator("#list .row").count() === 2, "filter Yours: the two presets saved here");
await page.click('#pfilter [data-f="all"]');

// ---- export, import ----
const [dl1] = await Promise.all([page.waitForEvent("download"), (async () => { await row(9).locator(".more").click(); await menuItem("Export"); })()]);
const one = JSON.parse(readFileSync(await dl1.path(), "utf8"));
ok(one.format === "openpocket-presets" && one.presets.length === 1 && one.presets[0].name === name10, `Export: ${dl1.suggestedFilename()}`);
const [dl2] = await Promise.all([page.waitForEvent("download"), page.click("#expall")]);
const all = JSON.parse(readFileSync(await dl2.path(), "utf8"));
ok(all.presets.length === 52 && all.presets[0].name === "MY BIG TONE" && all.presets[0].factory === false, `Back up all: 52 presets (${dl2.suggestedFilename()})`);
// a file of my own: two presets, renamed, into 30 and 01 (01 holds a preset of mine)
const mine = { format: "openpocket-presets", version: 1, presets: [
  { ...all.presets[0], slot: 30, name: "IMPORTED ONE", factory: false },
  { ...all.presets[8], slot: 1, name: "IMPORTED TWO", factory: false } ] };
const f2 = join(TMP, "two.json");
writeFileSync(f2, JSON.stringify(mine));
await page.setInputFiles("#file", f2);
await dialog();
ok(/replaces your MY BIG TONE/.test(await text("#dlg")) && /over factory/.test(await text("#dlg")) && /Import 2 presets/.test(await text("#dlg-ok")), "Import: the plan, preset by preset, with what each replaces");
await shot("editor_5_import.png");
await clearToasts();
await page.click("#dlg-ok");
ok(await toast(/2 presets imported/), "imported");
ok((await row(29).textContent()).includes("IMPORTED ONE") && (await row(0).textContent()).includes("IMPORTED TWO"), "the two presets are in 30 and 01");
await page.locator("#toasts .toast-a").click();
ok(await toast(/Undone: the import/), "Undo of the import");
ok((await row(0).textContent()).includes("MY BIG TONE") && !/Yours/.test(await row(29).textContent()), "01 is MY BIG TONE again, 30 factory again");
const f1 = join(TMP, "one.json");
writeFileSync(f1, JSON.stringify({ ...one, presets: [{ ...one.presets[0], name: "FROM A FILE" }] }));
await clearToasts();
await page.setInputFiles("#file", f1);
ok(await toast(/FROM A FILE is playing now/), "Import one preset: it becomes the current sound");
ok((await pedalState()).name === "FROM A FILE" && await text("#star") === "Edited", "playing, not saved anywhere yet");
const bad = join(TMP, "bad.json");
writeFileSync(bad, "{ not json");
await clearToasts();
await page.setInputFiles("#file", bad);
ok(await toast(/not an OpenPocket preset file/), "a file that is not a preset file is refused, with its name");
ok((await pedalState()).writesOk, "the flash was written only inside the preset store");
await page.click("#revert");
await sleep(200);

// (v37 NAM captures, v38 cabinet IRs and the TONE3000 import were tested here: the Models tab went with NAM in v70)

// ---- MIDI: the pedal's numbers, and a controller taught through the page ----
{
  await go("midi");
  ok(/Foot Controller/.test(await text("#midictl")) && /hold FX for 0\.8 s/.test(await text("#midiconn")), "MIDI: the controller the browser sees, and where the pedal's own settings are");
  const synRow = page.locator("#midimap tbody tr").filter({ has: page.locator("th span", { hasText: /^SYN$/ }) }).first();   /* (the rows follow the chain's order, not the block ids) */
  ok(/SYN/.test(await synRow.locator("th").textContent()) && /CC 23/.test(await synRow.textContent()) && /CC 55/.test(await synRow.textContent()) && /CC 50/.test(await synRow.textContent()), `the pedal's own map: SYN on/off 23, type 55, its 2nd setting 50: ${(await synRow.textContent()).replace(/\s+/g, " ")}`);
  await shot("editor_8_midi.png");
  await page.click('.cc-cell[data-t="param:3:1"]');
  ok(/Waiting for a control for SYN · /.test(await text("#learnstate")), "Learn: click a control, the page waits for the controller");
  await page.evaluate(() => window.__ctl([0xB0, 11, 100]));
  await sleep(150);
  ok(/SYN · .+ now follows CC 11 · ch 1 of Foot Controller, in every preset/.test(await text("#learnstate")), "a knob moved on the controller is learned");
  ok(/CC 11 · ch 1/.test(await text('.cc-cell[data-t="param:3:1"]')) && await page.locator("#learned .lrow").count() === 1, "the control shows it, and it is listed under Learned controls");
  await page.evaluate(() => window.__ctl([0xB0, 11, 127]));
  await sleep(400);
  ok((await pedalState()).amp[1] === 100, "the controller's knob at 127: the pedal's SYN 2nd setting is 100");
  await page.evaluate(() => window.__ctl([0xB0, 11, 32]));
  await sleep(400);
  ok((await pedalState()).amp[1] === 25, "at 32: 25 (the pedal's own scaling)");
  ok(/SYN · .+ 25/.test(await text("#monlist")), "the monitor shows what arrived and what it did");
  // a footswitch: each press flips the block
  await page.click('.cc-cell[data-t="on:7:0"]');
  await page.evaluate(() => window.__ctl([0xB0, 80, 127]));
  await sleep(200);
  const d0 = (await pedalState()).on[7];
  await page.evaluate(() => { window.__ctl([0xB0, 80, 0]); window.__ctl([0xB0, 80, 127]); });
  await sleep(400);
  ok((await pedalState()).on[7] === (d0 ? 0 : 1), "a footswitch learned for DLY on / off: a press flips it, the release does nothing");
  // only in this preset
  await page.selectOption("#midiscope", "preset");
  await page.click('.cc-cell[data-t="param:8:2"]');
  await page.evaluate(() => window.__ctl([0xB1, 12, 64]));
  await sleep(200);
  const here = (await pedalState()).slot;
  ok(new RegExp(`Only ${String(here + 1).padStart(2, "0")}`).test(await page.locator("#learned .lrow").nth(2).locator("select").last().evaluate((s) => s.selectedOptions[0].textContent)), "a control taught for this preset only says so");
  // Program Change and the preset steps
  await page.evaluate(() => window.__ctl([0xC0, 4]));
  await page.waitForFunction(() => document.getElementById("num").textContent === "05", null, { timeout: 3000 }).catch(() => {});
  ok((await pedalState()).slot === 4, "Program Change 4 from the controller loads preset 05");
  const rev = (await pedalState()).type[8];
  await page.evaluate(() => window.__ctl([0xB1, 12, 127]));
  await sleep(300);
  ok(/not learned/.test(await page.locator("#monlist li").first().textContent()), "the control taught for another preset does nothing here");
  await page.click('#midimap [data-t="preset:1"]');
  await page.evaluate(() => window.__ctl([0xB0, 81, 127]));
  await sleep(200);
  await page.evaluate(() => window.__ctl([0xB0, 81, 127]));
  await page.waitForFunction(() => document.getElementById("num").textContent === "06", null, { timeout: 3000 }).catch(() => {});
  ok((await pedalState()).slot === 5 && (await pedalState()).type[8] !== undefined && rev !== undefined, "a footswitch learned for Next preset goes to 06");
  await page.locator("#learned .lrow").first().locator(".btn.icon").click();
  ok(await page.locator("#learned .lrow").count() === 3, "Forget removes a learned control");
  ok(await page.evaluate(() => JSON.parse(localStorage.getItem("openpocket-midi-v1")).mappings.length) === 3, "the learned controls are kept in the browser");
  // what the pedal sends itself (SEND on): the monitor names it
  await page.evaluate(() => window.__simIn("b01740"));
  await sleep(200);
  ok(/Pedal/.test(await page.locator("#monlist li").first().textContent()) && /SYN on \/ off/.test(await page.locator("#monlist li").first().textContent()), "a Control Change the pedal sends is named in the monitor");
  await shot("editor_8_midi_learned.png");
}

// ---- the pedal unplugged, and back ----
{
  await go("sound");
  sim.stdin.end();
  await page.evaluate(() => window.__simGone());
  await page.waitForSelector('body[data-conn="lost"]', { timeout: 8000 });
  ok(await page.isVisible("#lostbar") && await page.locator("#shell").evaluate((e) => e.inert), "the pedal unplugged: said at the top, the editor held still");
  await shot("editor_9_lost.png");
  startSim();
  await page.waitForSelector('body[data-conn="ready"]', { timeout: 20000 });
  ok(await page.isHidden("#lostbar") && await page.locator("#shell").evaluate((e) => !e.inert), "plugged back in: connected again by itself");
}

// ---- v42: USB audio (pull request #25), in the MIDI section: off by default; the button asks, sets it, and the editor
// connects again by itself ----
{
  await go("midi");
  await page.waitForSelector("#uastate", { timeout: 8000 });
  ok(await page.textContent("#uastate") === "Off" && await page.textContent("#uabtn") === "Turn on", "USB audio: shown, off by default");
  await page.click("#uabtn");
  await dialog();
  ok(/Turn USB audio on\?/.test(await text("#dlg")) && /leaves USB/.test(await text("#dlg")), "USB audio: asks first, and says the pedal leaves USB");
  await page.click("#dlg-ok");
  const back = (state) => page.waitForFunction((t) => document.body.dataset.conn === "ready" &&
    (document.getElementById("uastate") || {}).textContent === t, state, { timeout: 20000 });
  await back("On");
  ok(await page.textContent("#uabtn") === "Turn off", "USB audio: on, and the editor connected again");
  await shot("editor_8_usb_audio.png");
  await page.click("#uabtn");
  await dialog();
  await page.click("#dlg-ok");
  await back("Off");
  ok(await page.textContent("#uabtn") === "Turn on", "USB audio: off again");
  await go("sound");
}

// ---- phone width ----
await page.setViewportSize({ width: 390, height: 844 });
await sleep(300);
for (const v of ["presets", "sound", "midi"]) {
  await go(v);
  await sleep(150);
  const sw = await page.evaluate(() => document.documentElement.scrollWidth);
  ok(sw <= 390, `phone width, ${v}: no sideways scroll (${sw} px)`);
  await shot(`editor_10_phone_${v}.png`);
}
await go("sound");
const tw = await page.locator("#tiles .tile .pw").first().boundingBox();
ok(tw.width >= 44 && tw.height >= 24, `phone: a block's switch is ${Math.round(tw.width)} x ${Math.round(tw.height)} px`);

await browser.close();
sim.stdin.end();
server.close();
console.log(failed ? `\n${failed} FAILED` : "\neditor page test passed");
process.exit(failed ? 1 : 0);
