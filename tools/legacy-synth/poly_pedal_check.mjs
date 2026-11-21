// OpenPocket poly-test smoke check on a connected pedal (read-mostly: plays temporary presets, then restores the
// current sound; never saves). Usage: node tools/poly_pedal_check.mjs [EXPECTED_IDENTITY]   e.g. BlackBox_958
// Reports the saved-preset hash, per-voicing peak DSP load with Smart Gate off/on, late blocks, second core, safety bypass.
import assert from "node:assert/strict";
import {createHash} from "node:crypto";
import {createRequire} from "node:module";
import {C, Editor, encodePreset} from "../../docs/pxedit.js";
import {nodeMidiAccess} from "../midi-node.mjs";
import {parsePing} from "../dsp.mjs";
const requireFromTools = createRequire(new URL("../package.json", import.meta.url));
const access = nodeMidiAccess(requireFromTools("@julusian/midi"));
const editor = new Editor(access);
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const ping = async () => parsePing(await editor.request(C.PING));
const gate = 0, syn = 11;
let original;
try {
    const info = await editor.connect();
    console.log(info?.identity, info?.version);
    if (process.argv[2]) assert.equal(info?.identity, process.argv[2]);
    const {blocks} = await editor.catalog();
    const lookup = (block, name) => {
        const type = blocks[block].types.findIndex(t => t?.name === name);
        assert.ok(type >= 0, name);
        return type;
    };
    const smart = lookup(gate, "SMART GATE");
    const specs = [
        ["PULSE POLY", ["WIDTH", "PWM", "CUTOFF", "RESO", "RELEASE"], [50, 50, 50, 50, 50]],
        ["SAW POLY", ["DETUNE", "OCTAVE", "CUTOFF", "SWEEP", "RELEASE"], [100, 50, 60, 100, 50]],
        ["ARCADE POLY", ["WAVE", "OCTAVE", "CRUSH", "DECAY", "MIX"], [20, 67, 100, 60, 100]],
        ["FM POLY", ["RATIO", "INDEX", "DECAY", "RELEASE", "LEVEL"], [100, 100, 100, 50, 100]],
        ["MONO SYNTH", null, [50, 50, 50, 50, 50]],          // v60: on the note layer in poly-test (load only)
        ["STRING ENSEMBLE", ["OCTAVE", "TONE", "LAYER", "RELEASE", "DRY"], [50, 50, 100, 50, 0]],   // v61
        ["BRASS ENSEMBLE", ["OCTAVE", "TONE", "LAYER", "RELEASE", "DRY"], [50, 50, 100, 50, 0]],
        ["STRING DUO", ["CELLO", "VIOLIN", "TONE", "ATTACK", "RELEASE"], [50, 50, 50, 30, 40]],   // v65
        ["CELLO ENSEMBLE", null, [50, 50, 50, 50, 0]],       // v61: the string ensemble's lead alone (load reference)
        ["TRUMPET", null, [50, 50, 50, 50, 0]],
        ["POLY OCT FUZZ", null, [100, 100, 50, 50, 50]],     // v62: the HQ octave bank (both voices up)
        ["STRINGS HQ", null, [50, 50, 50, 50, 50]],
    ].map(([name, params, knobs]) => {
        const type = lookup(syn, name);
        if (params) assert.deepEqual(blocks[syn].types[type].params.map(p => p.name), params);
        return {name, type, knobs, params};
    });
    const arcade = blocks[syn].types[specs[2].type];
    const fm = blocks[syn].types[specs[3].type];
    assert.equal(arcade.params[0].texts?.[0], "SQUARE");
    assert.equal(arcade.params[0].texts?.[100], "STEP PU");
    assert.equal(fm.params[0].texts?.[0], "1:1");
    assert.equal(fm.params[0].texts?.[100], "5:1");
    const saved = (await editor.list()).filter(slot => slot.saved);
    const sha = createHash("sha256");
    for (const {slot} of saved) {
        const {preset} = await editor.slotGet(slot);
        sha.update(Buffer.from([slot, ...encodePreset(preset, info.nblk, info.npar)]));
    }
    console.log(JSON.stringify({saved: saved.length, savedHash: sha.digest("hex"),
        types: specs.map(({name, type}) => ({name, type}))}));
    original = await editor.state();
    for (const {slot} of saved) {                       // v64: what each saved preset's PITCH and SYN types read as now
        const {preset} = await editor.slotGet(slot);
        console.log(JSON.stringify({slot, pitch: blocks[2].types[preset.type[2]]?.name, syn: blocks[syn].types[preset.type[syn]]?.name}));
    }
    if (process.argv[3] === "consolidated") {           // v63: BASS SYNTH (type 6) is retired; a preset with it plays HQ
        assert.ok(!blocks[syn].types[6]?.name, "BASS SYNTH hidden");
        const q = structuredClone(original.preset); q.type[syn] = 6; q.on[syn] = 1;
        assert.equal(await editor.put(q), 0);
        await pause(300);
        const got = (await editor.state()).preset.type[syn];
        assert.equal(blocks[syn].types[got]?.name, "BASS SYNTH HQ");
        console.log(JSON.stringify({migrated: "BASS SYNTH (6) -> " + blocks[syn].types[got].name + " (" + got + ")"}));
        const pit = 2;                                   // the PITCH block
        // v64: PWM SYNTH HQ (17) -> PWM SYNTH; PITCH POLY OCTAVE (1) -> POLY OCTAVE HQ with RANGE 0
        assert.ok(!blocks[syn].types[17]?.name && !blocks[pit].types[1]?.name, "PWM SYNTH HQ and POLY OCTAVE hidden");
        q.type[syn] = 17; q.type[pit] = 1; q.on[pit] = 1; q.p[pit] = [40, 70, 30, 88, 50];
        assert.equal(await editor.put(q), 0);
        await pause(300);
        const st = (await editor.state()).preset;
        assert.equal(blocks[syn].types[st.type[syn]]?.name, "PWM SYNTH");
        assert.equal(blocks[pit].types[st.type[pit]]?.name, "POLY OCTAVE HQ");
        assert.deepEqual(st.p[pit].slice(0, 4), [40, 70, 30, 0]);
        console.log(JSON.stringify({migrated: ["PWM SYNTH HQ -> PWM SYNTH", "POLY OCTAVE -> POLY OCTAVE HQ, RANGE 0"]}));
        const hq = blocks[pit].types.findIndex(t => t?.name === "POLY OCTAVE HQ");
        assert.deepEqual(blocks[pit].types[hq].params.map(p => p.name), ["DRY", "OCT -1", "OCT +1", "RANGE", "OCT -2"]);
        const lowest = blocks[pit].types.findIndex(t => t?.name === "LOWEST OCTAVE HQ");   // v54's lowest-note mode
        assert.ok(lowest >= 0, "LOWEST OCTAVE HQ present");
        for (const [type, knobs] of [[hq, [50, 100, 0, 0, 0]], [hq, [50, 0, 0, 0, 100]], [hq, [50, 100, 100, 0, 100]],
                                     [lowest, [50, 100, 0, 0, 0]]]) {
            const r = structuredClone(original.preset); r.on.fill(0); r.order = r.order.map((_, i) => i);
            r.on[pit] = 1; r.type[pit] = type; r.p[pit] = knobs; r.p[0][2] = 50;
            assert.equal(await editor.put(r), 0);
            await pause(650);
            const a = await ping(); let peak = a.peak, b = a, core = a.secondCore, safe = a.bypass;
            for (let i = 0; i < 10; i++) { await pause(500); b = await ping(); peak = Math.max(peak, b.peak); core &&= b.secondCore; safe ||= b.bypass; }
            const late = (b.xrun - a.xrun + 16384) % 16384;
            console.log(JSON.stringify({name: blocks[pit].types[type].name + " " + knobs.join(","), peak, pitch: b.prof?.[pit], secondCore: core, safety: safe, late}));
            assert.ok(core && !safe && late === 0);
        }
    }
    const temporary = ({type, knobs}, gateOn) => {
        const q = structuredClone(original.preset);
        q.on.fill(0);
        q.order = q.order.map((_, i) => i);
        q.on[syn] = 1;
        q.type[syn] = type;
        q.p[syn] = [...knobs];
        q.p[gate][2] = 50;
        if (gateOn) {
            q.on[gate] = 1;
            q.type[gate] = smart;
            q.p[gate] = [25, 35, 50, 25, 50];
        }
        return q;
    };
    for (const gateOn of [false, true]) for (const spec of specs) {
        assert.equal(await editor.put(temporary(spec, gateOn)), 0);
        console.log(`${spec.name}, Smart Gate ${gateOn ? "on" : "off"} for 5 seconds`);
        await pause(650);
        const start = await ping();
        let end = start, peak = start.peak, secondCore = start.secondCore, safety = start.bypass;
        for (let i = 0; i < 10; i++) {
            await pause(500);
            end = await ping();
            peak = Math.max(peak, end.peak);
            secondCore &&= end.secondCore;
            safety ||= end.bypass;
        }
        const late = (end.xrun - start.xrun + 16384) % 16384;
        console.log(JSON.stringify({name: spec.name, gateOn, peak, secondCore, safety, late, synth: end.prof?.[syn]}));
        assert.ok(secondCore && !safety && late === 0);
    }
    for (const spec of specs.slice(1, 4)) {
        assert.equal(await editor.put(temporary(spec, false)), 0);
        await pause(650);
        const start = await ping();
        for (const value of [0, 100, 50]) {
            for (let k = 0; k < 5; ++k) await editor.setParam(syn, k, value);
            assert.deepEqual((await editor.state()).preset.p[syn], Array(5).fill(value));
        }
        await editor.setOn(syn, false);
        await pause(150);
        assert.equal((await editor.state()).preset.on[syn], 0);
        await editor.setOn(syn, true);
        await pause(150);
        assert.equal((await editor.state()).preset.on[syn], 1);
        const end = await ping();
        const late = (end.xrun - start.xrun + 16384) % 16384;
        console.log(JSON.stringify({name: spec.name, controls: "all five endpoints verified", bypass: "off/on verified",
            late, secondCore: end.secondCore, safety: end.bypass}));
        assert.ok(end.secondCore && !end.bypass && late === 0);
    }
} finally {
    try {
        if (original) {
            assert.equal(await editor.put(original.preset), 0);
            const after = await editor.state();
            assert.equal(after.slot, original.slot);
            assert.deepEqual(after.preset, original.preset);
            console.log("Original sound restored and read back.");
        }
    } finally {
        editor.close();
        access.close();
    }
}
