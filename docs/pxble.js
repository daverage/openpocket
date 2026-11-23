// SPDX-License-Identifier: GPL-3.0-only
// OpenPocket — the editor's transport over Bluetooth Low Energy (Web Bluetooth), for phones whose browser has no Web MIDI
// for a Bluetooth MIDI device. It gives pxedit.js the shape of a Web MIDI access ({ inputs, outputs }, one port each way):
// the editor's SysEx messages (docs/EDITOR-PROTOCOL.md) travel unchanged inside a GATT service of our own. Written from the
// Bluetooth Core and Web Bluetooth specifications, not from any other firmware or app.
//
// The service has two characteristics:
//   RX  (the pedal receives)  Write Without Response: the browser writes the request, in pieces of at most the ATT payload
//   TX  (the pedal sends)     Notify: the reply and the pushed STATE frames, in pieces
// A message is one SysEx, F0 ... F7. Its data bytes are 7-bit, so F0 and F7 mark its ends inside any piece: no framing of
// our own is added. A piece may hold the end of one message and the start of the next, or part of one. Bytes outside a
// message are dropped; F0 inside a message starts it again (a message cut off by a lost link).

export const BLE_SERVICE = "d0422e58-4744-4a69-9ddc-ce8f5e7d2a16";
export const BLE_RX = "4d6bfa59-0314-4186-961d-1cf96d870b41";     // written by the browser
export const BLE_TX = "d8d0081c-e149-4719-ad32-9c374aa8d01e";     // notified by the pedal
export const BLE_NAME = "OpenPocket (Bluetooth)";                  // matches pxota.js PORT_RE
export const DEFAULT_PIECE = 20;                                    // ATT's default payload (MTU 23 - 3); the safe size
export const MAX_REQUEST = 256;                                     // the longest request the pedal's buffer takes (the editor's are about 160)
export const MAX_REPLY = 4096;                                      // the longest reply the page takes (the catalogue's are up to about 850)

// the bytes of a message in pieces of at most `size`
export function splitMessage(msg, size = DEFAULT_PIECE) {
  const d = msg instanceof Uint8Array ? msg : Uint8Array.from(msg);
  if (!(size >= 1)) throw new Error("piece size must be at least 1");
  const out = [];
  for (let i = 0; i < d.length; i += size) out.push(d.subarray(i, Math.min(d.length, i + size)));
  return out;
}

// pieces in, whole messages out. onMessage gets a Uint8Array F0 ... F7.
export class Reassembler {
  constructor(onMessage, max = MAX_REPLY) {
    this.onMessage = onMessage;
    this.max = max;
    this.buf = [];
    this.open = false;
    this.dropped = 0;           // bytes that belonged to no message, or to one too long
  }
  reset() { this.buf = []; this.open = false; }
  push(piece) {
    const d = piece instanceof Uint8Array ? piece : Uint8Array.from(piece);
    for (let i = 0; i < d.length; i++) {
      const b = d[i];
      if (b === 0xF0) { if (this.open) this.dropped += this.buf.length; this.buf = [b]; this.open = true; }
      else if (!this.open) this.dropped++;
      else if (b === 0xF7) {
        this.buf.push(b);
        const m = Uint8Array.from(this.buf);
        this.reset();
        this.onMessage(m);
      } else if (b & 0x80) {    // a status byte inside a SysEx: it is not one of ours
        this.dropped += this.buf.length + 1; this.reset();
      } else if (this.buf.length >= this.max) { this.dropped += this.buf.length + 1; this.reset(); }
      else this.buf.push(b);
    }
  }
}

// The access object pxedit.js's Editor takes. `link` is the GATT side (a real one from connectDevice, or a fake in tests):
//   link.write(Uint8Array): resolves when the piece is handed to the radio (writes are made one after the other)
//   link.onNotify = (Uint8Array) => {}: set by this object
//   link.onDisconnect = () => {}: set by this object
//   link.reconnect(): resolves when the link is back
// opts: piece (bytes per write, default 20), pace (ms between writes, default 0), name
export function makeAccess(link, opts = {}) {
  const piece = opts.piece || DEFAULT_PIECE;
  const pace = opts.pace || 0;
  const name = opts.name || BLE_NAME;
  const access = { inputs: new Map(), outputs: new Map(), onstatechange: null, link, stats: { writes: 0, bytesOut: 0, messagesIn: 0 } };
  let state = "connected";
  let chain = Promise.resolve();
  const reasm = new Reassembler((m) => {
    access.stats.messagesIn++;
    if (input.onmidimessage) input.onmidimessage({ data: m });
  });
  const changed = (port) => { if (access.onstatechange) { try { access.onstatechange({ port }); } catch (_) { } } };
  const input = { name, type: "input", get state() { return state; }, onmidimessage: null, open: async () => input, close: async () => {} };
  const output = {
    name, type: "output", get state() { return state; }, open: async () => output, close: async () => {},
    // Web MIDI's send() is not awaited: the pieces are queued and written in order
    send(bytes) {
      if (state !== "connected") throw new Error("Bluetooth link is down");
      const pieces = splitMessage(bytes, piece);
      chain = chain.then(async () => {
        for (const p of pieces) {
          if (state !== "connected") return;
          await link.write(p);
          access.stats.writes++; access.stats.bytesOut += p.length;
          if (pace) await new Promise((r) => setTimeout(r, pace));
        }
      }).catch(() => {});
    },
  };
  link.onNotify = (d) => reasm.push(d);
  link.onDisconnect = () => {
    if (state === "disconnected") return;
    state = "disconnected"; reasm.reset(); chain = Promise.resolve();
    changed(input); changed(output);
  };
  // after a lost link (phones sleep the radio): the same ports come back, and editor.js finds the pedal again
  access.reconnect = async () => {
    await link.reconnect();
    state = "connected"; reasm.reset();
    changed(input); changed(output);
  };
  access.disconnect = () => { try { link.disconnect && link.disconnect(); } catch (_) { } };
  input.access = output.access = access;
  access.inputs.set(name, input); access.outputs.set(name, output);
  access.reasm = reasm;
  return access;
}

// Where Web Bluetooth can work: a secure page, and a browser that has it (Chrome and Edge on Android and the desktop, Bluefy on iOS).
export function bluetoothSupport(nav = globalThis.navigator, secure = globalThis.isSecureContext) {
  if (!nav || !nav.bluetooth) return { ok: false, why: "no-web-bluetooth" };
  if (secure === false) return { ok: false, why: "insecure" };
  return { ok: true };
}

// A user gesture (a click) must start this. The browser shows its own device picker.
export async function requestBluetoothAccess(opts = {}) {
  const sup = bluetoothSupport();
  if (!sup.ok) { const e = new Error(sup.why); e.code = sup.why; throw e; }
  const device = await navigator.bluetooth.requestDevice({
    filters: [{ services: [BLE_SERVICE] }],
    optionalServices: [BLE_SERVICE],
  });
  const link = await connectDevice(device);
  return makeAccess(link, opts);
}

// The GATT side on a real device
export async function connectDevice(device) {
  const link = { device, onNotify: null, onDisconnect: null, tx: null, rx: null };
  async function attach() {
    const server = await device.gatt.connect();
    const svc = await server.getPrimaryService(BLE_SERVICE);
    link.rx = await svc.getCharacteristic(BLE_RX);
    link.tx = await svc.getCharacteristic(BLE_TX);
    link.tx.addEventListener("characteristicvaluechanged", (ev) => {
      const v = ev.target.value;
      if (link.onNotify) link.onNotify(new Uint8Array(v.buffer, v.byteOffset, v.byteLength));
    });
    await link.tx.startNotifications();
  }
  device.addEventListener("gattserverdisconnected", () => { if (link.onDisconnect) link.onDisconnect(); });
  await attach();
  link.write = (p) => link.rx.writeValueWithoutResponse(p);
  link.reconnect = attach;
  link.disconnect = () => { if (device.gatt.connected) device.gatt.disconnect(); };
  return link;
}
