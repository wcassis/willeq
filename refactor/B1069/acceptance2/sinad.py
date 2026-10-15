import sys, glob, os, numpy as np
from scipy.io import wavfile
from scipy.signal import get_window
def sinad(x, fs, f0):
    x = x[len(x)//4: len(x)//4 + 65536] if len(x) > 65536*1.3 else x[len(x)//4:]
    w = get_window(('kaiser', 20), len(x)); X = np.abs(np.fft.rfft(x * w)) ** 2
    fr = np.fft.rfftfreq(len(x), 1 / fs); k = np.argmin(np.abs(fr - f0)); bw = 12
    sig = X[max(0, k-bw):k+bw+1].sum(); X2 = X.copy(); X2[max(0,k-bw):k+bw+1] = 0; X2[:3] = 0
    j = np.argmax(X2)
    return 10*np.log10(sig / X2.sum()), fr[j], 10*np.log10(X2[j] / sig)
for p in sorted(glob.glob(os.path.join(sys.argv[1], "tone_*.wav")), key=lambda s: int(s.split('_')[-1].split('.')[0]) if not s.endswith('mix.wav') else 0):
    if p.endswith(".mix.wav"): continue
    f0 = int(os.path.basename(p).split('_')[1].split('.')[0])
    rd, d = wavfile.read(p); rm, m = wavfile.read(p + ".mix.wav")
    dl = d[:, 0].astype(np.float64) / (32768.0 if d.dtype == np.int16 else 1.0); ml = m[:, 0].astype(np.float64)
    s_d, fsp_d, sp_d = sinad(dl, rd, f0); s_m, fsp_m, sp_m = sinad(ml, rm, f0)
    dd = np.abs(np.diff(dl)); md = np.abs(np.diff(ml))
    print(f"{f0:5d} Hz  mix SINAD {s_m:6.1f} dB | device SINAD {s_d:6.1f} dB, worst spur {fsp_d:7.0f} Hz at {sp_d:6.1f} dBc | peak dev {np.abs(dl).max():.4f} mix {np.abs(ml).max():.4f} | len ratio {len(d)/len(m):.5f}")
