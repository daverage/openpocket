// v49 diagnosis: which SYN type takes the second core down? The current sound (not saved) gets each type in turn on
// its SYN block for SEC seconds while you play; after each, PING says whether the second core still runs. The sound
// the pedal had is put back at the end. Read-only for the flash.  node tools/core2_probe.mjs [SEC]
import { C, Editor } from "../docs/pxedit.js";
import { parsePing } from "./dsp.mjs";
const sec = Number(process.argv[2] || 8);
const midi = await import("@julusian/midi").then((m) => m.default || m);
const access = (await import("./midi-node.mjs")).nodeMidiAccess(midi);
const ed = new Editor(access);
await ed.connect();
const cat = await ed.catalog();
const SYN = 11, syn = cat.blocks[SYN];
const want = (process.env.TYPES || "TRUMPET,BOWED CELLO,CELLO ENSEMBLE,BASS SYNTH HQ,PWM SYNTH HQ,STRINGS HQ,POLY OCT FUZZ,POLY BRASS,PITCH:POLY OCTAVE HQ").split(",");   // (PITCH:name: a PITCH type)
const st = await ed.state(), was = st.preset;
const sleep = (s) => new Promise((r) => setTimeout(r, s * 1000));
let p = parsePing(await ed.request(C.PING));
console.log(`start: core 2 ${p.secondCore ? "on" : "OFF"}, load ${p.load}%`);
try {
  for (const name of want) {
    const [full, pv] = name.split("@");                   // (NAME@a/b/c/d/e: its five parameters)
    const pitch = full.startsWith("PITCH:"), blk = pitch ? 2 : SYN, nm = pitch ? full.slice(6) : full;
    const t = cat.blocks[blk].types.findIndex((x) => x && x.name === nm);
    if (t < 0) { console.log(`${name}: not in this firmware`); continue; }
    const q = structuredClone(was);
    q.on[blk] = 1; q.type[blk] = t; q.p[blk] = pv ? pv.split("/").map(Number) : pitch ? [50, 100, 100, 0, 0] : [50, 50, 50, 50, 0];
    if (pitch) q.on[SYN] = 0;
    for (const b of [5, 6]) q.on[b] = 0;                   // AMP and CAB off: the whole engine then runs on core 2
    await ed.put(q);
    console.log(`${name}: play now (${sec} s)`);
    let peak = 0;
    for (let i = 0; i < sec * 2; i++) { await sleep(0.5); p = parsePing(await ed.request(C.PING)); peak = Math.max(peak, p.load); if (!p.secondCore) break; }
    console.log(`  -> core 2 ${p.secondCore ? "on" : "OFF"}, load peak ${peak}%`);
    if (!p.secondCore) break;
  }
} finally { await ed.put(was); console.log("your sound is back"); access.close?.(); process.exit(0); }
