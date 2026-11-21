import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from rm_circuit import RM
FS = 96000.0
def gain(f, amp, **kw):
    r = RM(fs=FS, **kw); n = int(FS * (0.02 + 10 / f)); t = np.arange(n) / FS; e = amp * np.sin(2 * np.pi * f * t); y = r.run(e)
    seg = y[int(FS * 0.01):]; ts = t[int(FS * 0.01):]; k = int(len(seg) / (FS / f)) * int(FS / f) if False else len(seg)
    c = np.exp(-2j * np.pi * f * ts); z = 2 * np.abs((seg * c).mean()); return 20 * np.log10(z / amp + 1e-12), seg.mean()
if __name__ == '__main__':
    print('ideal source (Rs 1 ohm, L ~0), vol max, 1 mV in:  frequency -> gain dB')
    print(' '.join(f'{f}:{gain(f, 1e-3, Rs=1.0, L=1e-6)[0]:.1f}' for f in (100, 300, 1000, 2600, 5000, 10000)))
    print('single coil (Rs 6k, L 2.5 H), 1 mV EMF:')
    print(' '.join(f'{f}:{gain(f, 1e-3)[0]:.1f}' for f in (100, 300, 1000, 2600, 5000, 10000)))
    print('humbucker (Rs 8k, L 5 H), 1 mV EMF:')
    print(' '.join(f'{f}:{gain(f, 1e-3, Rs=8000.0, L=5.0)[0]:.1f}' for f in (100, 300, 1000, 2600, 5000, 10000)))
