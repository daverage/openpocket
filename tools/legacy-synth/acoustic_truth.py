#!/usr/bin/env python3
"""Host prototype (docs/research/REALISTIC-INSTRUMENT-PROTOTYPE.md): note-level ground truth for the bundled lead loop.

  python3 tools/legacy-synth/acoustic_truth.py [OUTDIR]        default OUTDIR: build/acoustic

assets/loops/lead.wav is directinput_12.wav of Guitar-TECHS P3_music from 3.275 s to 13.566 s (assets/loops/prepare.py).
The dataset's midi/midi_12.mid is a transcription of the same performance (its onsets are not on a grid, and one lands
exactly on the loop's start), so its notes in that window, shifted to the loop's time, are a reference for what was
played. Downloads P3_music.zip (130 MB, CC BY 4.0, https://zenodo.org/records/14963133) into OUTDIR/gtechs if missing
and writes OUTDIR/lead.truth.csv (start s, end s, MIDI note, velocity) for tools/legacy-synth/acoustic_voice.c.
"""
import hashlib, os, struct, sys, urllib.request, zipfile

URL = "https://zenodo.org/records/14963133/files/P3_music.zip"
START, END = 3.275, 13.566                    # the lead loop's window in the take (assets/loops/prepare.py)


def read_midi(data):
    """Note on / off pairs of a standard MIDI file, in seconds (one tempo, as these files have)."""
    assert data[:4] == b"MThd"
    _, ntracks, division = struct.unpack(">HHH", data[8:14])
    p, tempo, events = 14, 500000, []

    def varlen(q):
        v = 0
        while True:
            b = data[q]; q += 1; v = (v << 7) | (b & 127)
            if b < 128: return v, q

    for _ in range(ntracks):
        assert data[p:p + 4] == b"MTrk"
        length = struct.unpack(">I", data[p + 4:p + 8])[0]
        q, end, tick, status, held = p + 8, p + 8 + length, 0, 0, {}
        while q < end:
            dt, q = varlen(q); tick += dt
            if data[q] & 0x80: status = data[q]; q += 1
            if status == 0xFF:
                kind = data[q]; q += 1; n, q = varlen(q)
                if kind == 0x51: tempo = int.from_bytes(data[q:q + 3], "big")
                q += n
            elif status in (0xF0, 0xF7):
                n, q = varlen(q); q += n
            else:
                hi = status & 0xF0
                if hi in (0x80, 0x90):
                    note, vel = data[q], data[q + 1]; q += 2
                    if hi == 0x90 and vel > 0: held[note] = (tick, vel)
                    elif note in held:
                        t0, v0 = held.pop(note); events.append((t0, tick, note, v0))
                elif hi in (0xC0, 0xD0): q += 1
                else: q += 2
        p = end
    sec = tempo / 1e6 / division
    return sorted((a * sec, b * sec, n, v) for a, b, n, v in events)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "build/acoustic"
    d = os.path.join(out, "gtechs"); os.makedirs(d, exist_ok=True)
    z = os.path.join(d, "P3_music.zip")
    if not os.path.exists(z):
        print("download", URL)
        with urllib.request.urlopen(URL, timeout=120) as r, open(z + ".part", "wb") as f:
            while True:
                b = r.read(1 << 20)
                if not b: break
                f.write(b)
        os.replace(z + ".part", z)
    sha = hashlib.sha256(open(z, "rb").read()).hexdigest()
    with zipfile.ZipFile(z) as zf:
        notes = read_midi(zf.read("P3_music/midi/midi_12.mid"))
    rows = [(a - START, min(b, END) - START, n, v) for a, b, n, v in notes if START - 0.005 <= a < END]
    fn = os.path.join(out, "lead.truth.csv")
    with open(fn, "w") as f:
        f.write(f"# Guitar-TECHS P3_music midi_12.mid, {START}..{END} s, zip sha256 {sha}\n")
        for a, b, n, v in rows: f.write(f"{a:.4f},{b:.4f},{n},{v}\n")
    print(f"{len(rows)} notes -> {fn}")


if __name__ == "__main__":
    main()
