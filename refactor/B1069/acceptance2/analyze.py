import sys, numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly, welch
dev_path = sys.argv[1]; period = int(sys.argv[2]) if len(sys.argv) > 2 else 1115
rd, d = wavfile.read(dev_path); rm, m = wavfile.read(dev_path + ".mix.wav")
if d.dtype == np.int16: dclip = int(np.sum((d == 32767) | (d == -32768))); d = d.astype(np.float64) / 32768.0
else: dclip = int(np.sum(np.abs(d) >= 1.0)); d = d.astype(np.float64)
m = m.astype(np.float64)
mclip = int(np.sum(np.abs(m) >= 1.0))
print(f"device {rd} Hz {d.shape[0]} frames; mix {rm} Hz {m.shape[0]} frames")
print(f"length ratio dev/mix = {d.shape[0]/m.shape[0]:.6f} (ideal {rd/rm:.6f})")
print(f"peak dev {np.max(np.abs(d)):.4f} mix {np.max(np.abs(m)):.4f}; rms dev {np.sqrt(np.mean(d**2)):.4f} mix {np.sqrt(np.mean(m**2)):.4f}")
print(f"clipped samples: dev {dclip} ({100*dclip/d.size:.3f} %), mix {mclip} ({100*mclip/m.size:.3f} %)")
# reference: band-limited resample of the mix to the device rate
g = np.gcd(rd, rm); ref = resample_poly(m, rd // g, rm // g, axis=0)
n = min(len(ref), len(d)) - 200
best = None
for lag in range(-8, 9):
    a = d[100 + lag:100 + lag + n - 200]; b = ref[100:100 + n - 200]
    e = np.mean((a - b) ** 2)
    if best is None or e < best[0]: best = (e, lag)
lag = best[1]
a = d[100 + lag:100 + lag + n - 200]; b = ref[100:100 + n - 200]
err = a - b
snr = 10 * np.log10(np.mean(b ** 2) / np.mean(err ** 2))
print(f"vs band-limited reference: lag {lag} frames, SNR {snr:.1f} dB, max |err| {np.max(np.abs(err)):.4f}")
# boundary discontinuities: first-difference at period boundaries vs elsewhere
dd = np.abs(np.diff(d[:, 0]))
idx = np.arange(period, len(dd), period) - 1
bnd = dd[idx]; other = np.delete(dd, idx)
print(f"|first diff| L: at period boundaries max {bnd.max():.4f} p99 {np.percentile(bnd,99):.4f}; elsewhere max {other.max():.4f} p99 {np.percentile(other,99):.4f}")
md = np.abs(np.diff(m[:, 0])); print(f"mix |first diff| L max {md.max():.4f} p99 {np.percentile(md,99):.4f}")
# imaging/aliasing: energy above the mix's Nyquist in the device stream
f, p = welch(d[:, 0], fs=rd, nperseg=8192)
tot = np.sum(p); hi = np.sum(p[f > rm / 2])
print(f"device energy above {rm/2:.0f} Hz: {10*np.log10(hi/tot):.1f} dB rel. total")
# zero runs (underrun-like gaps) in device stream where the mix is not silent
z = np.all(d == 0, axis=1); runs = 0; cur = 0; longest = 0
for v in z:
    cur = cur + 1 if v else 0; longest = max(longest, cur)
print(f"longest all-zero run in device stream: {longest} frames")
