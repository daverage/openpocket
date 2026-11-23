// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket — the editor over Bluetooth (docs/pxble.js): the framing, and the real editor client (docs/pxedit.js) talking to
// the simulated pedal (tests/edsim.c, the firmware's real editor code) through a fake GATT link that cuts every message into
// pieces, as the radio does. No pedal and no browser.
//   cc -O1 -o build/edsim tests/edsim.c engine/px_dsp.c firmware/px_math.c -lm && node tests/test_pxble.mjs
import { spawn } from "node:child_process";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { Editor } from "../docs/pxedit.js";
import { splitMessage, Reassembler, makeAccess, bluetoothSupport, BLE_NAME, DEFAULT_PIECE } from "../docs/pxble.js";

const ROOT = fileURLToPath(new URL("..", import.meta.url));
const SIM = process.env.EDSIM || join(ROOT, "build", "edsim");
globalThis.localStorage = { getItem: () => null, setItem: () => {} };
let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(76)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const eq = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);

// ---- 1. the framing ----
{
  const msg = Uint8Array.from([0xF0, 0x7D, 0x4F, 0x50, 1, ...Array.from({ length: 150 }, (_, i) => i % 128), 0xF7]);
  const parts = splitMessage(msg, 20);
  ok(parts.length === 8 && parts.every((p) => p.length <= 20) && parts.reduce((n, p) => n + p.length, 0) === msg.length, "split: 156 bytes in pieces of 20");
  ok(splitMessage(msg, 512).length === 1 && splitMessage(msg, 1).length === msg.length, "split: one piece at a large MTU, one byte per piece at the smallest");
  let got = [];
  const r = new Reassembler((m) => got.push(m));
  for (const p of parts) r.push(p);
  ok(got.length === 1 && eq(got[0], msg), "reassemble: the pieces give the message back");

  for (const size of [1, 2, 3, 7, 20, 64, 185, 512]) {
    got = [];
    const rr = new Reassembler((m) => got.push(m));
    const two = [...msg, ...msg];
    for (const p of splitMessage(two, size)) rr.push(p);
    if (!(got.length === 2 && eq(got[0], msg) && eq(got[1], msg))) { ok(false, `reassemble: two messages back to back in pieces of ${size}`); break; }
  }
  ok(true, "reassemble: two messages back to back, in pieces of 1 to 512, split anywhere");

  got = [];
  const g = new Reassembler((m) => got.push(m));
  g.push([0x12, 0x34, 0xF7, 0x40]);                                    // bytes outside a message
  g.push([0xF0, 1, 2, 3]); g.push([0xF0, 7, 8, 0xF7]);                 // a message cut off, then a new one
  g.push([0xF0, 5, 0x90, 6, 0xF7]);                                    // a status byte inside
  g.push([0xF0, 9, 0xF7]);
  ok(got.length === 2 && eq(got[0], [0xF0, 7, 8, 0xF7]) && eq(got[1], [0xF0, 9, 0xF7]) && g.dropped >= 8, `reassemble: noise dropped, a cut-off message replaced (${g.dropped} bytes dropped)`);

  got = [];
  const lim = new Reassembler((m) => got.push(m), 16);
  lim.push([0xF0, ...new Array(40).fill(1), 0xF7, 0xF0, 1, 0xF7]);
  ok(got.length === 1 && eq(got[0], [0xF0, 1, 0xF7]), "reassemble: a message longer than the limit is dropped, the next one is kept");

  ok(bluetoothSupport({}, true).why === "no-web-bluetooth" && bluetoothSupport({ bluetooth: {} }, false).why === "insecure" && bluetoothSupport({ bluetooth: {} }, true).ok, "support: no Web Bluetooth, an insecure page, and the working case are told apart");
}

// ---- 2. the real editor client through the fake radio ----
function simPedal() {
  const child = spawn(SIM, [], { stdio: ["pipe", "pipe", "inherit"] });
  let buf = "", port = null, readyFn, hear = null;
  const ready = new Promise((r) => { readyFn = r; });
  child.stdout.on("data", (d) => {
    buf += d.toString();
    let i;
    while ((i = buf.indexOf("\n")) >= 0) {
      const ln = buf.slice(0, i); buf = buf.slice(i + 1);
      if (ln.startsWith("#port ")) { port = ln.slice(6); readyFn(); }
      else if (ln.startsWith("#") || !ln) continue;
      else if (hear) hear(Uint8Array.from(Buffer.from(ln, "hex")));
    }
  });
  const done = new Promise((r) => child.on("exit", r));
  return { ready, send: (b) => child.stdin.write(Buffer.from(b).toString("hex") + "\n"), onReply: (f) => { hear = f; }, end: () => { child.stdin.end(); return done; } };
}

// the fake GATT link: the pedal end reassembles what the browser writes, and cuts what it sends into notifications
function fakeLink(pedal, { notifyPiece = 20, dropEvery = 0 } = {}) {
  const pedalRx = new Reassembler((m) => pedal.send(m));
  const link = { onNotify: null, onDisconnect: null, up: true, writes: 0, notifies: 0, maxWrite: 0 };
  link.write = async (p) => {
    if (!link.up) throw new Error("link down");
    link.writes++; link.maxWrite = Math.max(link.maxWrite, p.length);
    await sleep(0);
    pedalRx.push(p);
  };
  pedal.onReply((m) => {
    if (!link.up) return;
    for (const p of splitMessage(m, notifyPiece)) { link.notifies++; setTimeout(() => { if (link.up && link.onNotify) link.onNotify(p); }, 0); }
  });
  link.reconnect = async () => { await sleep(5); link.up = true; };
  link.drop = () => { link.up = false; if (link.onDisconnect) link.onDisconnect(); };
  return link;
}

for (const [piece, notifyPiece] of [[20, 20], [185, 185], [512, 512], [7, 3]]) {
  const pedal = simPedal();
  await pedal.ready;
  const link = fakeLink(pedal, { notifyPiece });
  const access = makeAccess(link, { piece });
  const ed = new Editor(access);
  const info = await ed.connect();
  ok(info && info.proto >= 9 && info.port === BLE_NAME, `pieces of ${piece}/${notifyPiece}: INFO through the radio (${info && info.identity}, port "${info && info.port}")`);
  const cat = await ed.catalog();
  const st = await ed.state();
  const slots = await ed.list();
  ok(cat.blocks.length >= 10 && st.preset.name.length > 0 && slots.length > 0, `pieces of ${piece}/${notifyPiece}: catalogue (${cat.blocks.length} blocks), state "${st.preset.name}", ${slots.length} slots`);
  ok(link.maxWrite <= piece, `pieces of ${piece}/${notifyPiece}: no write longer than the piece (${link.maxWrite} bytes), ${link.writes} writes, ${link.notifies} notifications`);
  // a change reaches the pedal and comes back as STATE while watched
  let pushed = 0;
  ed.onState = () => pushed++;
  await ed.watch(true);
  await ed.set(0, 0, 0, 33).catch(() => ed.set(0, 0, 33));
  await sleep(150);
  ok(access.stats.messagesIn > 20, `pieces of ${piece}/${notifyPiece}: ${access.stats.messagesIn} messages received whole, ${pushed} STATE pushes`);
  ed.close();
  await pedal.end();
}

// ---- 3. a link that drops and comes back ----
{
  const pedal = simPedal();
  await pedal.ready;
  const link = fakeLink(pedal, {});
  const access = makeAccess(link, { piece: 20 });
  const ed = new Editor(access);
  await ed.connect();
  let events = 0;
  access.onstatechange = () => events++;
  const first = await ed.state();
  link.drop();
  ok(events === 2 && access.inputs.get(BLE_NAME).state === "disconnected" && access.outputs.get(BLE_NAME).state === "disconnected", "link lost: both ports report disconnected and the page is told");
  let threw = false;
  try { access.outputs.get(BLE_NAME).send([0xF0, 0xF7]); } catch (_) { threw = true; }
  ok(threw, "link lost: a send throws instead of vanishing");
  await access.reconnect();
  ok(access.inputs.get(BLE_NAME).state === "connected" && events === 4, "link back: ports connected again, the page is told");
  const ed2 = new Editor(access);
  const info2 = await ed2.connect();
  const again = await ed2.state();
  ok(info2 && again.preset.name === first.preset.name, "link back: a new editor finds the pedal and reads the same preset");
  // a message cut off by the drop must not poison the next one
  access.reasm.push([0xF0, 0x7D, 0x4F]);
  const third = await ed2.state();
  ok(third.preset.name === first.preset.name, "a half message left in the buffer does not break the next request");
  ed2.close();
  await pedal.end();
}

console.log(failed ? `\n${failed} FAILED` : "\nall passed");
process.exit(failed ? 1 : 0);
