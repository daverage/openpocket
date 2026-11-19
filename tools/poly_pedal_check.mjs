// OpenPocket poly-test smoke check on a connected pedal (read-mostly: plays temporary presets, then restores the
// current sound; never saves). Usage: node tools/poly_pedal_check.mjs [EXPECTED_IDENTITY]   e.g. BlackBox_958
// Reports the saved-preset hash, per-voicing peak DSP load with Smart Gate off/on, late blocks, second core, safety bypass.
import assert from "node:assert/strict";
import {createHash} from "node:crypto";
import {createRequire} from "node:module";
import {C, Editor, encodePreset} from "../docs/pxedit.js";
import {nodeMidiAccess} from "./midi-node.mjs";
import {parsePing} from "./dsp.mjs";
const requireFromTools = createRequire(new URL("./package.json", import.meta.url));
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
