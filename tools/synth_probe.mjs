// v70 stage 4: the DSP load of every SYNTH type, alone and in a full chain (GATE, OCTAVE HQ, SYNTH, MOD, DELAY, REVERB),
// as the current sound (not saved; the sound the pedal had is put back). PING after SEC seconds: load, peak, late
// blocks, bypass, second core, and the busiest blocks.  node tools/synth_probe.mjs [SEC]   (TYPES=A,B to pick)
import { C, Editor } from "../docs/pxedit.js";
import { parsePing } from "./dsp.mjs";
const sec = Number(process.argv[2] || 4);
const midi = await import("@julusian/midi").then((m) => m.default || m);
const access = (await import("./midi-node.mjs")).nodeMidiAccess(midi);
const ed = new Editor(access);
const info = await ed.connect();
if (!info) { access.close(); throw new Error("no OpenPocket pedal"); }
const cat = await ed.catalog();
const B = Object.fromEntries(cat.blocks.map((b, i) => [b.label, i]));
const typeOf = (b, n) => cat.blocks[b].types.findIndex((t) => t && t.name === n);
const sleep = (s) => new Promise((r) => setTimeout(r, s * 1000));
const was = (await ed.state()).preset;
const wanted = process.env.TYPES ? process.env.TYPES.split(",") : cat.blocks[B.SYN].types.map((t) => t.name);
console.log(`${info.identity} ${info.version}; ${sec} s each, no guitar playing`);
console.log("type                   alone: load peak late  | chain: load peak late byp 2nd  busiest");
const one = async (q) => {
  await ed.put(q); await sleep(sec);
  const a = parsePing(await ed.request(C.PING)); await sleep(1); const b = parsePing(await ed.request(C.PING));
  return { load: b.load, peak: b.peak, late: (b.xrun - a.xrun + 16384) % 16384, byp: b.bypass, c2: b.secondCore, prof: b.prof };
};
try {
  for (const name of wanted) {
    const t = typeOf(B.SYN, name);
    if (t < 0) continue;
    const q = structuredClone(was);
    q.on.fill(0); q.on[B.GT] = 1; q.p[B.GT] = [10, 20, 50, 0, 0];
    q.on[B.SYN] = 1; q.type[B.SYN] = t; q.p[B.SYN] = [50, 50, 50, 50, 50];
    q.order = cat.blocks.map((_, i) => i);
    const a = await one(q);
    q.on[B.OCT] = 1; q.type[B.OCT] = typeOf(B.OCT, "POLY OCTAVE HQ"); q.p[B.OCT] = [50, 100, 100, 0, 0];
    q.on[B.MOD] = 1; q.type[B.MOD] = 0; q.p[B.MOD] = [30, 40, 40, 30, 50];
    q.on[B.DLY] = 1; q.type[B.DLY] = 0; q.p[B.DLY] = [28, 30, 25, 50, 30];
    q.on[B.REV] = 1; q.type[B.REV] = 1; q.p[B.REV] = [50, 50, 20, 40, 0];
    const c = await one(q);
    const busy = (c.prof || []).map((v, i) => [cat.blocks[i].label, v]).filter(([, v]) => v).sort((x, y) => y[1] - x[1]).slice(0, 3).map(([l, v]) => `${l} ${v}`).join(" ");
    console.log(`${name.padEnd(22)} ${String(a.load).padStart(5)}% ${String(a.peak).padStart(3)}% ${String(a.late).padStart(4)}  | ${String(c.load).padStart(5)}% ${String(c.peak).padStart(3)}% ${String(c.late).padStart(4)} ${c.byp ? "BYP" : " - "} ${c.c2 ? " on" : "OFF"}  ${busy}`);
  }
} finally { await ed.put(was).catch(() => {}); ed.close(); access.close(); }
