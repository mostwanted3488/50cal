#!/usr/bin/env python3
# Mesures objectives des effets F.RANGE (sans écoute) : silence, bruit, repliement, stabilité, RT60, clics.
import subprocess, os, sys, numpy as np
from scipy import signal
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
CLI = os.path.join(HERE, '..', 'build', 'frange_cli')
OUT = os.path.join(HERE, 'out'); os.makedirs(OUT, exist_ok=True)
FS = 48000
results = []

def check(name, ok, detail=''):
    results.append((name, ok, detail)); print(('PASS ' if ok else 'FAIL ') + name + ('  — ' + detail if detail else ''))

def run(fx, x, fs=FS, tail=0, **kw):
    """x : (n,) ou (n,2). retourne (n+tail*fs, 2)"""
    x = np.asarray(x, dtype=np.float32)
    if x.ndim == 1: x = np.stack([x, x], 1)
    fin, fout = os.path.join(OUT, '_in.f32'), os.path.join(OUT, '_out.f32')
    x.astype('<f4').tofile(fin)
    args = [CLI, fx, str(fs), fin, fout, f'tail={tail}']
    for k, v in kw.items():
        if k == 'events':
            for t, a in v: args.append(f'at={t}:active={a}')
        else: args.append(f'{k}={v}')
    subprocess.run(args, check=True)
    y = np.fromfile(fout, dtype='<f4').reshape(-1, 2)
    return y

def db(x): return 20 * np.log10(np.maximum(np.abs(x), 1e-12))
def sine(f, a, n, fs=FS): return a * np.sin(2 * np.pi * f * np.arange(n) / fs)

# ============================ DRIVE ============================
print('\n=== DRIVE ===')
z = np.zeros(2 * FS)
y = run('drive', z, drive=1.0, bias=1.0, tone=1.0, mix=1.0)
check('drive: silence -> silence exact (drive max)', np.max(np.abs(y)) == 0.0, f'max={np.max(np.abs(y)):.1e}')

imp = np.zeros(FS); imp[1000] = 1.0
y = run('drive', imp, mix=0.0)
pk = int(np.argmax(np.abs(y[:, 0])))
check('drive: latence annoncée = 52 échantillons (dry aligné)', pk == 1000 + 52 and abs(y[pk, 0] - 1.0) < 1e-5, f'pic à +{pk-1000}, valeur {y[pk,0]:.6f}')
imp2 = np.zeros(FS); imp2[1000] = 1e-3
yw = run('drive', imp2, drive=0.0, bias=0.0, mix=1.0)[:, 0]
# position fractionnaire du pic (interpolation parabolique)
j = int(np.argmax(np.abs(yw))); a, b, c = np.abs(yw[j - 1]), np.abs(yw[j]), np.abs(yw[j + 1]); frac = 0.5 * (a - c) / (a - 2 * b + c)
check('drive: chemin traité aligné sur le dry (latence 52 ±0.3 ech.)', abs((j + frac - 1000) - 52) < 0.3, f'latence mesurée {j + frac - 1000:.2f}')

# réponse petit signal (drive 0) : plate ?
gains = []
for f in [60, 120, 250, 500, 1000, 2000, 4000, 8000, 12000, 16000]:
    x = sine(f, 0.01, FS)
    y = run('drive', x, drive=0.0, bias=0.0, tone=0.0)
    seg = slice(FS // 2, FS)
    gains.append(20 * np.log10(np.sqrt(np.mean(y[seg, 0]**2)) / np.sqrt(np.mean(x[seg]**2))))
gains = np.array(gains)
check('drive: réponse petit signal plate (±0.5 dB autour de la valeur à 1 kHz, 100 Hz–16 kHz)',
      np.max(np.abs(gains[1:] - gains[4])) < 0.5, 'gains dB: ' + ' '.join(f'{g:+.2f}' for g in gains))

# harmoniques
def spectrum(x, fs=FS):
    w = signal.windows.blackmanharris(len(x)); X = np.fft.rfft(x * w) / (np.sum(w) / 2)
    return np.fft.rfftfreq(len(x), 1 / fs), np.abs(X)
x = sine(1000, 0.25, FS)
fig, axs = plt.subplots(1, 3, figsize=(15, 3.6), sharey=True)
for ax, drv in zip(axs, [0.3, 0.6, 1.0]):
    y = run('drive', x, drive=drv, bias=0.35)
    seg = y[FS // 2: FS // 2 + 32768, 0]
    f, X = spectrum(seg)
    ax.plot(f / 1000, db(X)); ax.set_xlim(0, 24); ax.set_ylim(-140, 6); ax.set_title(f'drive={drv}  moyenne(DC)={np.mean(seg):+.1e}'); ax.set_xlabel('kHz'); ax.grid(alpha=.3)
    h = [X[int(np.argmin(np.abs(f - 1000 * k)))] for k in range(1, 8)]
    print(f'   drive={drv}: harmoniques (dB rel. fond.) ' + ' '.join(f'H{k+1}={db(h[k]/h[0]):+.0f}' for k in range(1, 7)))
    check(f'drive={drv}: pas de continu (|DC| < -80 dBFS)', abs(np.mean(seg)) < 1e-4, f'DC={np.mean(seg):+.2e}')
axs[0].set_ylabel('dBFS'); plt.tight_layout(); plt.savefig(os.path.join(OUT, 'drive_harmonics.png'), dpi=110); plt.close()

# repliement : on compare au même algorithme tournant à 4x la fréquence d'échantillonnage (référence quasi sans repliement)
def alias_metric(y, f0, fs=FS):
    seg = y[FS // 2: FS // 2 + 65536]
    f, X = spectrum(seg, fs)
    mask = np.ones_like(f, bool)
    k = 1
    while k * f0 < fs / 2:
        mask &= np.abs(f - k * f0) > 40; k += 1
    mask &= (f > 30) & (f < 20000)     # seul le repliement AUDIBLE (< 20 kHz) compte
    inharm = np.sqrt(np.sum(X[mask] ** 2))
    return 20 * np.log10(inharm / X[int(np.argmin(np.abs(f - f0)))])
for f0 in [3517.3, 7123.9, 9913.7]:
    n = FS + 70000
    x = sine(f0, 0.5, n)
    y = run('drive', x, drive=1.0, bias=0.35)[:, 0]
    xr = sine(f0, 0.5, 4 * n, fs=4 * FS)
    yr = run('drive', xr, fs=4 * FS, drive=1.0, bias=0.35)[:, 0]
    hdec = signal.firwin(801, 21500, window=('kaiser', 12.0), fs=4 * FS)
    yr = signal.resample_poly(yr, 1, 4, window=hdec)
    naive = np.tanh(x * 10 ** (38 / 20) + 0.315) - np.tanh(0.315)
    a_ours, a_ref, a_naive = alias_metric(y, f0), alias_metric(yr, f0), alias_metric(naive, f0)
    print(f'   f0={f0:.0f} Hz : parasites inharmoniques  F.RANGE {a_ours:.1f} dB | référence 16x {a_ref:.1f} dB | sans sur-échantillonnage {a_naive:.1f} dB')
    check(f'drive: repliement audible f0={f0:.0f} Hz (drive MAX) < -40 dB et ≥ 25 dB mieux que sans anti-repliement',
          a_ours < -40 and a_ours < a_naive - 25, f'{a_ours:.1f} dB (naïf {a_naive:.1f})')
    y5 = run('drive', x, drive=0.5, bias=0.35)[:, 0]; n5 = np.tanh(x * 10 ** (19 / 20) + 0.315) - np.tanh(0.315)
    a5, a5n = alias_metric(y5, f0), alias_metric(n5, f0)
    print(f'      drive 0.5 : F.RANGE {a5:.1f} dB | sans anti-repliement {a5n:.1f} dB')
    check(f'drive: repliement audible f0={f0:.0f} Hz (drive 0.5) < -60 dB', a5 < -60, f'{a5:.1f} dB')

# clics à l'activation
n = 2 * FS; x = sine(997, 0.4, n)
def steady_ref(y, a0, a1): return np.max(np.abs(np.diff(y[a0:a1, 0])))
y = run('drive', x, drive=0.8, mix=1.0, events=[(1.0, 0)])
d = np.abs(np.diff(y[:, 0])); win = d[FS - 200: FS + 1200].max(); ref = max(steady_ref(y, FS // 2, FS - 2400), steady_ref(y, FS + 4800, 2 * FS - 100))
check('drive: pas de clic à la désactivation (saut max pendant la transition ≤ 1.15 × saut max du signal stable)', win <= 1.15 * ref, f'transition {win:.4f} vs stable {ref:.4f}')
y2 = run('drive', x, drive=0.8, mix=1.0, active=0, events=[(1.0, 1)])
d2 = np.abs(np.diff(y2[:, 0])); win2 = d2[FS - 200: FS + 1200].max(); ref2 = max(steady_ref(y2, FS // 2, FS - 2400), steady_ref(y2, FS + 4800, 2 * FS - 100))
check('drive: pas de clic à l\'activation', win2 <= 1.15 * ref2, f'transition {win2:.4f} vs stable {ref2:.4f}')

# niveau compensé
rng = np.random.default_rng(1)
t = np.arange(FS * 2) / FS
mix_sig = sum(np.sin(2 * np.pi * f * t + rng.uniform(0, 6)) for f in [110, 233, 470, 940, 1870, 3500]) * 0.06
lv = []
for drv in [0.0, 0.25, 0.5, 0.75, 1.0]:
    y = run('drive', mix_sig, drive=drv)[FS // 2:, 0]
    lv.append(20 * np.log10(np.sqrt(np.mean(y ** 2)) / np.sqrt(np.mean(mix_sig[FS // 2:] ** 2))))
print('   niveau de sortie vs entrée selon drive 0/.25/.5/.75/1 (dB): ' + ' '.join(f'{v:+.1f}' for v in lv))
check('drive: niveau de sortie quasi constant sur toute la plage (±3 dB, comparaison à niveau égal)', max(abs(v) for v in lv) < 2.5)

yl = run('drive', x[:FS], drive=0.9)
check('drive: entrée mono -> sorties L=R identiques', np.array_equal(yl[:, 0], yl[:, 1]))
check('drive: aucune valeur non finie, crête < 2.0', np.all(np.isfinite(yl)) and np.max(np.abs(yl)) < 2.0, f'crête {np.max(np.abs(yl)):.3f}')

# ============================ REVERB ============================
print('\n=== REVERB ===')
y = run('reverb', np.zeros(FS), mix=1.0, decay=12.0)
check('reverb: silence -> silence exact', np.max(np.abs(y)) == 0.0)

def ir(**kw):
    n = int(0.1 * FS); x = np.zeros(n); x[int(0.05 * FS)] = 0.5
    return run('reverb', x, tail=kw.pop('tail', 16.0), mix=1.0, **kw)
def rt60(h, fs=FS, lo=-5, hi=-35):
    e = np.cumsum((h ** 2)[::-1])[::-1]; e = 10 * np.log10(e / e[0] + 1e-30)
    i0 = np.argmax(e <= lo); i1 = np.argmax(e <= hi)
    if i1 <= i0: return float('nan')
    return 2.0 * (i1 - i0) / fs   # extrapolation 30 dB -> 60 dB
IRs = {}
for decay in [1.2, 2.8, 6.0]:
    h = ir(decay=decay, damp=0.55); IRs[decay] = h
    sos_lo = signal.butter(4, 500, 'low', fs=FS, output='sos'); sos_hi = signal.butter(4, 4000, 'high', fs=FS, output='sos')
    mono = h[:, 0]
    r_all, r_lo, r_hi = rt60(mono), rt60(signal.sosfilt(sos_lo, mono)), rt60(signal.sosfilt(sos_hi, mono))
    print(f'   decay={decay}s : RT60 large bande {r_all:.2f}s | <500 Hz {r_lo:.2f}s | >4 kHz {r_hi:.2f}s')
    check(f'reverb decay={decay}: RT60 graves proche de la consigne (±25 %)', abs(r_lo - decay) / decay < 0.25, f'{r_lo:.2f}s')
    check(f'reverb decay={decay}: aigus plus courts que graves (sombre, damp=.55)', r_hi < 0.8 * r_lo, f'{r_hi:.2f}s < {r_lo:.2f}s')
h = IRs[2.8]
tailsil = np.max(np.abs(h[-FS:]))
check('reverb: queue arrive au silence (dernière seconde < -120 dBFS)', tailsil < 1e-6, f'{db(tailsil):.0f} dBFS')
check('reverb: pas de continu dans la queue', abs(np.mean(h[int(0.5 * FS):, 0])) < 1e-6)
e = np.cumsum((h[:, 0] ** 2)[::-1])[::-1]
env = np.array([np.sum(h[i:i + 2400, 0] ** 2) for i in range(0, len(h) - 2400, 2400)])
env = env[env > 1e-14]
check('reverb: énergie décroît (pas de croissance/instabilité)', np.all(np.diff(10 * np.log10(env[3:] + 1e-30)) < 3.0))
# densité / peignes
seg = h[int(0.4 * FS):int(1.2 * FS), 0]
w = signal.windows.hann(len(seg)); S = 20 * np.log10(np.abs(np.fft.rfft(seg * w)) + 1e-12)
fr = np.fft.rfftfreq(len(seg), 1 / FS); sel = (fr > 300) & (fr < 8000)
sm = signal.savgol_filter(S[sel], 51, 1)
dev = np.std(S[sel] - sm)
print(f'   écart-type du spectre de queue (bruit gaussien idéal ≈ 5.6 dB): {dev:.1f} dB')
check('reverb: queue lisse, pas de résonances/peignes marqués (écart-type < 8 dB)', dev < 8.0, f'{dev:.1f} dB')
# stéréo
a = h[int(0.5 * FS):int(2.0 * FS)]
cc = np.corrcoef(a[:, 0], a[:, 1])[0, 1]
check('reverb: largeur stéréo (corrélation L/R de la queue |c| < 0.3)', abs(cc) < 0.3, f'c={cc:.3f}')
hw = ir(decay=2.8, width=0.0, tail=3.0)
a = hw[int(0.5 * FS):int(2.0 * FS)]
check('reverb: width=0 -> mono parfait', np.max(np.abs(a[:, 0] - a[:, 1])) < 1e-6)

# pré-délai
hp_ = ir(decay=2.0, pre=50.0, tail=1.0)
on = int(np.argmax(np.abs(hp_[:, 0]) > 1e-7)) - int(0.05 * FS)
check('reverb: pré-délai 50 ms respecté (1er signal ≥ 50 ms)', on >= int(0.049 * FS), f'{1000*on/FS:.1f} ms')

# stabilité extrême
rng = np.random.default_rng(3)
bur = rng.uniform(-1, 1, 2 * FS)
ys = run('reverb', bur, tail=40.0, mix=1.0, decay=12.0, size=1.0, damp=0.0, mod=1.0, warmth=1.0, lowcut=20, highcut=20000)
pk = np.max(np.abs(ys)); r10 = np.sqrt(np.mean(ys[12 * FS:14 * FS] ** 2)); r30 = np.sqrt(np.mean(ys[40 * FS:42 * FS] ** 2))
check('reverb: stable aux réglages extrêmes (fini, crête < 10, décroissance continue)', np.all(np.isfinite(ys)) and pk < 10 and r30 < r10, f'crête {pk:.2f}, RMS {r10:.2e} -> {r30:.2e}')

# niveau relatif wet / dry
ns = rng.standard_normal(4 * FS) * 0.1
ns = signal.lfilter([1], [1, -0.9], ns)
yw = run('reverb', ns, mix=1.0, decay=2.8)
lvl = 20 * np.log10(np.sqrt(np.mean(yw[FS:4 * FS] ** 2)) / np.sqrt(np.mean(ns[FS:] ** 2)))
print(f'   niveau wet (mix=100 %) vs entrée : {lvl:+.1f} dB')
check('reverb: niveau wet cohérent (-9 .. +3 dB vs entrée)', -9 < lvl < 3, f'{lvl:+.1f} dB')

# clics / bypass
x = sine(997, 0.4, 3 * FS)
yb = run('reverb', x, tail=8.0, mix=0.35, decay=2.0, events=[(1.0, 0)])
d = np.abs(np.diff(yb[:3 * FS, 0])); slope_ref = 2 * np.pi * 997 * 0.4 / FS
check('reverb: pas de clic au désactivation', d.max() < 1.6 * slope_ref, f'{d.max():.4f} vs {slope_ref:.4f}')
yb2 = run('reverb', np.concatenate([x, np.zeros(10 * FS)]), mix=0.35, decay=2.0, events=[(1.0, 0)])
resid = np.max(np.abs(yb2[int(11.5 * FS):, 0] - np.concatenate([x, np.zeros(10 * FS)])[int(11.5 * FS):]))
check('reverb: désactivé = transparent (sortie = entrée une fois la queue éteinte)', resid < 1e-6, f'écart {resid:.1e}')
yb3 = run('reverb', x, mix=0.35, decay=2.0, active=0, events=[(1.0, 1)])
d3 = np.abs(np.diff(yb3[:, 0]))
check('reverb: pas de clic à l\'activation', d3.max() < 1.6 * slope_ref, f'{d3.max():.4f}')

# figure
fig, axs = plt.subplots(1, 3, figsize=(16, 3.8))
t = np.arange(len(IRs[2.8])) / FS
for dcy, hh in IRs.items():
    e = np.cumsum((hh[:, 0] ** 2)[::-1])[::-1]; axs[0].plot(t, 10 * np.log10(e / e[0] + 1e-30), label=f'decay {dcy}s')
axs[0].set_ylim(-70, 3); axs[0].set_xlim(0, 10); axs[0].set_title('Courbe de Schroeder'); axs[0].legend(); axs[0].grid(alpha=.3); axs[0].set_xlabel('s')
f, tt, Sxx = signal.spectrogram(IRs[2.8][:, 0], FS, nperseg=2048, noverlap=1536)
im = axs[1].pcolormesh(tt, f / 1000, 10 * np.log10(Sxx + 1e-20), vmin=-140, vmax=-50, shading='auto'); axs[1].set_ylim(0, 20); axs[1].set_xlim(0, 6)
axs[1].set_title('Spectrogramme de la réponse (decay 2.8 s)'); axs[1].set_xlabel('s'); axs[1].set_ylabel('kHz'); plt.colorbar(im, ax=axs[1])
axs[2].plot(t[:int(0.5 * FS)] * 1000, IRs[2.8][:int(0.5 * FS), 0], lw=.6); axs[2].plot(t[:int(0.5 * FS)] * 1000, IRs[2.8][:int(0.5 * FS), 1], lw=.6, alpha=.7)
axs[2].set_title('Début de la réponse (réflexions précoces)'); axs[2].set_xlabel('ms'); axs[2].grid(alpha=.3)
plt.tight_layout(); plt.savefig(os.path.join(OUT, 'reverb_analysis.png'), dpi=110); plt.close()

nf = sum(1 for r in results if not r[1])
print(f'\n{len(results) - nf}/{len(results)} tests OK' + (f'  —  {nf} ECHEC(S)' if nf else ''))
sys.exit(1 if nf else 0)
