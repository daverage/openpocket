import sys, sounddevice as sd, soundfile as sf, numpy as np
src, dst = sys.argv[1], sys.argv[2]
x, sr = sf.read(src, dtype='float32', always_2d=True); x = x[:, 0]
x = np.concatenate([np.zeros(int(0.3*sr), np.float32), x, np.zeros(int(1.5*sr), np.float32)])
out = np.zeros((len(x), 2), np.float32); out[:, 0] = x
dev = next((i for i, d in enumerate(sd.query_devices()) if 'NUX' in d['name'] and d['max_input_channels'] >= 2 and d['max_output_channels'] >= 2), None)
if dev is None: sys.exit('the NUX interface is not connected')
rec = sd.playrec(out, sr, device=(dev, dev), channels=2, dtype='float32'); sd.wait()
sf.write(dst, rec[:, 0], sr, subtype='PCM_24')
