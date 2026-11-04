#!/usr/bin/env python3
# Mesures objectives des 6 autres effets F.RANGE : delay, chorus, crush, filter, phaser, flanger.
import os, sys, numpy as np
from scipy import signal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util
HERE = os.path.dirname(os.path.abspath(__file__))
import subprocess
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
CLI = os.path.join(HERE, '..', 'build', 'frange_cli'); OUT = os.path.join(HERE, 'out'); os.makedirs(OUT, exist_ok=True)
FS = 48000; results = []
def check(name, ok, detail=''):
    results.append((name, ok, detail)); print(('PASS ' if ok else 'FAIL ') + name + ('  — ' + detail if detail else ''))
def run(fx, x, fs=FS, tail=0, **kw):
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
    return np.fromfile(fout, dtype='<f4').reshape(-1, 2)
def db(x): return 20 * np.log10(np.maximum(np.abs(x), 1e-12))
def sine(f, a, n, fs=FS): return a * np.sin(2 * np.pi * f * np.arange(n) / fs)
rng = np.random.default_rng(11)

def common(fx, extreme, loud=None):
    """tests communs à chaque effet"""
    y = run(fx, np.zeros(2 * FS), **extreme)
    check(f'{fx}: silence -> silence exact (réglages extrêmes)', np.max(np.abs(y)) == 0.0, f'max={np.max(np.abs(y)):.1e}')
    x = rng.uniform(-1, 1, 3 * FS) * 0.9
    y = run(fx, x, tail=6.0, **extreme)
    check(f'{fx}: valeurs finies et crête < 12 aux réglages extrêmes (bruit pleine échelle)', np.all(np.isfinite(y)) and np.max(np.abs(y)) < 12, f'crête {np.max(np.abs(y)):.2f}')
    ten = y[-FS:]; check(f'{fx}: pas de continu ajouté dans la queue', abs(np.mean(ten)) < 1e-3)
    # clics à l'activation / désactivation
    x = sine(997, 0.4, 3 * FS)
    ref = np.max(np.abs(np.diff(run(fx, x, **loud or {})[FS // 2:FS, 0])))
    for lab, kw, ev in [('désactivation', dict(), [(1.0, 0)]), ('activation', dict(active=0), [(1.0, 1)])]:
        y = run(fx, x, events=ev, **{**(loud or {}), **kw})
        d = np.abs(np.diff(y[:, 0])); win = d[FS - 300: FS + 1500].max()
        check(f'{fx}: pas de clic à l\'{lab} (transition ≤ 1.3 × saut stable)', win <= 1.3 * max(ref, 2 * np.pi * 997 * 0.4 / FS), f'{win:.4f} vs {ref:.4f}')
    # inactif = transparent
    y = run(fx, np.concatenate([x[:FS], np.zeros(8 * FS)]), active=0, **(loud or {}))
    z = np.concatenate([x[:FS], np.zeros(8 * FS)])
    check(f'{fx}: désactivé = transparent (sortie = entrée)', np.max(np.abs(y[:, 0] - z)) < 1e-6, f'écart {np.max(np.abs(y[:,0]-z)):.1e}')

# ============================ DELAY ============================
print('\n=== DELAY ===')
common('delay', dict(time=1500, fb=0.92, warmth=1.0, mod=1.0, pp=1.0, tone=12000, duck=0.0, mix=1.0), loud=dict(time=250, mix=0.4))
imp = np.zeros(int(0.2 * FS)); imp[int(0.05 * FS)] = 0.6
y = run('delay', imp, tail=3.0, time=300, fb=0.5, mod=0.0, pp=0.0, mix=1.0, duck=0.0, warmth=0.0, tone=18000, lowcut=20)
e = np.abs(y[:, 0]); pk = []
for k in range(1, 5):
    c = int(0.05 * FS + k * 0.3 * FS); w = e[c - 150:c + 150]; pk.append((np.argmax(w) - 150 + c, np.max(w)))
errs = [abs((p[0] - 0.05 * FS) / FS * 1000 - 300 * (i + 1)) for i, p in enumerate(pk)]
check('delay: temps des échos exact (≤ 1.5 ms d\'écart sur 4 répétitions)', max(errs) < 1.5, ' '.join(f'{x:.2f}ms' for x in errs))
ratio = pk[1][1] / pk[0][1]
check('delay: chaque écho ≈ feedback × précédent (0.5 ±25 %, filtre inclus)', 0.35 < ratio < 0.65, f'rapport {ratio:.2f}')
yp = run('delay', imp, tail=2.0, time=300, fb=0.6, mod=0.0, pp=1.0, mix=1.0, duck=0.0)
l1 = np.max(np.abs(yp[int(0.34 * FS):int(0.37 * FS), 0])); r1 = np.max(np.abs(yp[int(0.34 * FS):int(0.37 * FS), 1]))
l2 = np.max(np.abs(yp[int(0.64 * FS):int(0.67 * FS), 0])); r2 = np.max(np.abs(yp[int(0.64 * FS):int(0.67 * FS), 1]))
check('delay: ping-pong alterne G/D', l1 > 5 * r1 and r2 > 5 * l2, f'écho1 G={l1:.3f} D={r1:.3f} | écho2 G={l2:.3f} D={r2:.3f}')
ys = run('delay', np.concatenate([imp, np.zeros(FS)]), tail=0, time=500, bpm=120, sync=2, mod=0.0, pp=0.0, mix=1.0, duck=0.0, warmth=0.0)[:, 0]
c = int(0.05 * FS + 0.25 * FS); w = np.abs(ys[c - 200:c + 200]); lag = (np.argmax(w) - 200) / FS * 1000
check('delay: synchro tempo 120 BPM, 1/8 = 250 ms', abs(lag) < 1.5, f'écart {lag:.2f} ms')
yd = run('delay', np.concatenate([imp, np.zeros(14 * FS)]), tail=0, time=400, fb=0.6, mix=1.0, mod=0.0, duck=0.0)
check('delay: les échos s\'éteignent jusqu\'au silence (dernière seconde < -120 dBFS)', np.max(np.abs(yd[-FS:])) < 1e-6, f'{db(np.max(np.abs(yd[-FS:]))):.0f} dBFS')
burst = np.concatenate([rng.uniform(-.5, .5, FS // 2), np.zeros(2 * FS)])
y0 = run('delay', burst, time=250, fb=0.3, mix=0.5, duck=0.0, mod=0.0); y1 = run('delay', burst, time=250, fb=0.3, mix=0.5, duck=1.0, mod=0.0)
seg = slice(int(0.3 * FS), int(0.5 * FS)); dryg = np.cos(0.5 * 0.5 * np.pi / 2)
w0 = y0[seg, 0] - burst[seg] * dryg; w1 = y1[seg, 0] - burst[seg] * dryg
red = db(np.sqrt(np.mean(w0 ** 2))) - db(np.sqrt(np.mean(w1 ** 2)))
check('delay: ducking baisse l\'écho pendant que le son joue (≥ 6 dB)', red > 6, f'{red:.1f} dB')

# ============================ CHORUS ============================
print('\n=== CHORUS ===')
common('chorus', dict(rate=5.0, depth=1.0, width=1.0, mix=1.0, tone=16000), loud=dict(mix=0.5))
x = sine(1000, 0.5, 2 * FS)
y = run('chorus', x, mix=0.0)
check('chorus: mix=0 -> sortie identique à l\'entrée (bit-exact)', np.array_equal(y[:, 0], x.astype(np.float32)))
y = run('chorus', x, rate=0.6, depth=1.0, mix=1.0, width=0.8)
an = signal.hilbert(y[FS // 2:, 0]); inst = np.diff(np.unwrap(np.angle(an))) * FS / (2 * np.pi)
dev = max(abs(np.percentile(inst, 90) - 1000), abs(np.percentile(inst, 10) - 1000)) / 1000 * 100
print(f'   chorus profondeur max : déviation de hauteur instantanée ±{dev:.2f} %')
check('chorus: déviation de hauteur (90e percentile) < 2 % à profondeur 100 % (pas de vibrato marqué)', dev < 2.0, f'{dev:.2f} %')
check('chorus: stéréo élargie (G≠D)', np.max(np.abs(y[FS // 2:, 0] - y[FS // 2:, 1])) > 0.01)
n = rng.standard_normal(6 * FS) * 0.1
yn = run('chorus', n, mix=0.5)
l0 = db(np.sqrt(np.mean(n ** 2))); l1 = db(np.sqrt(np.mean(yn[:, 0] ** 2)))
check('chorus: niveau conservé (±2 dB)', abs(l1 - l0) < 2.0, f'{l1 - l0:+.2f} dB')

# ============================ CRUSH ============================
print('\n=== CRUSH ===')
common('crush', dict(bits=3, rate=48, smooth=1500, mix=1.0), loud=dict(bits=8, rate=4, mix=1.0))
x = sine(1000, 0.9, FS)
for b in [6, 8, 10, 12]:
    y = run('crush', x, bits=b, rate=1, smooth=20000)[:, 0]
    err = y[2000:-2000] - x[2000:-2000]
    snr = 10 * np.log10(np.mean(x[2000:-2000] ** 2) / np.mean(err ** 2))
    th = 6.02 * b + 1.76 - 20 * np.log10(1 / 0.9) * 0 - 0.9  # sinus 0.9 FS
    print(f'   {b} bits : SNR mesuré {snr:.1f} dB (théorie ≈ {6.02*b+1.76-0.9:.1f} dB)')
    check(f'crush: {b} bits — rapport signal/quantification proche de la théorie (±5 dB)', abs(snr - (6.02 * b + 1.76 - 0.9)) < 5.0)
tiny = sine(1000, 0.002, FS); y = run('crush', tiny, bits=8, rate=1, smooth=20000)[:, 0]
check('crush: pas de dither -> signal sous 1/2 pas de quantification = silence exact (comportement voulu, aucun bruit)', np.max(np.abs(y)) == 0.0)
yr = run('crush', sine(3000, 0.5, FS), bits=16, rate=8, smooth=20000)[:, 0]
f, X = signal.periodogram(yr[FS // 4:], FS, window='hann'); X = 10 * np.log10(X + 1e-20)
img = 6000 - 3000  # image attendue à fs/8 - f = 3 kHz ... on vérifie la présence de répliques
check('crush: réduction de fréquence crée bien des répliques (image spectrale audible)', np.sum(10 ** (X[(f > 3200) & (f < 20000)] / 10)) > 1e-3 * np.sum(10 ** (X / 10)))

# ============================ FILTER ============================
print('\n=== FILTER ===')
common('filter', dict(cutoff=60, res=1.0, drive=1.0, mix=1.0, env=1.0), loud=dict(cutoff=2000, mix=1.0))
def gain_at(f, **kw):
    x = sine(f, 0.05, FS); y = run('filter', x, **kw)[:, 0]
    return 20 * np.log10(np.sqrt(np.mean(y[FS // 2:] ** 2)) / np.sqrt(np.mean(x[FS // 2:] ** 2)))
g = {f: gain_at(f, cutoff=1000, res=0.0, drive=0.0, mode=0) for f in [100, 500, 1000, 2000, 4000, 8000]}
print('   LP24 réponse (fc=1 kHz, rés 0) : ' + ' | '.join(f'{f} Hz {v:+.1f} dB' for f, v in g.items()))
check('filter: LP24 plat sous la coupure (±1 dB à 100 Hz)', abs(g[100]) < 1.0)
check('filter: LP24 pente ≈ 24 dB/octave (-18..-30 dB entre 2 et 4 kHz)', -34 < (g[4000] - g[2000]) < -16, f'{g[4000]-g[2000]:+.1f} dB/oct')
g12 = (gain_at(4000, cutoff=1000, res=0.0, drive=0.0, mode=1) - gain_at(2000, cutoff=1000, res=0.0, drive=0.0, mode=1))
check('filter: LP12 pente ≈ 12 dB/octave', -16 < g12 < -8, f'{g12:+.1f} dB/oct')
gh = gain_at(100, cutoff=1000, res=0.0, drive=0.0, mode=3) , gain_at(8000, cutoff=1000, res=0.0, drive=0.0, mode=3)
check('filter: HP24 coupe les graves et laisse passer les aigus', gh[0] < -40 and abs(gh[1]) < 1.5, f'100 Hz {gh[0]:.1f} dB, 8 kHz {gh[1]:.1f} dB')
gp = [gain_at(f, cutoff=1000, res=0.3, drive=0.0, mode=2) for f in [100, 1000, 8000]]
check('filter: BP passe le centre plus que les extrémités', gp[1] > gp[0] + 10 and gp[1] > gp[2] + 10, ' '.join(f'{v:+.1f}' for v in gp))
gr = gain_at(1000, cutoff=1000, res=0.9, drive=0.0, mode=0) - gain_at(1000, cutoff=1000, res=0.0, drive=0.0, mode=0)
check('filter: la résonance crée un pic à la coupure (> +10 dB)', gr > 10, f'{gr:+.1f} dB')
x = sine(120, 0.3, FS) + sine(1500, 0.3, FS)
ye = run('filter', x, cutoff=300, res=0.2, env=1.0, mode=0)[:, 0]; yn_ = run('filter', x, cutoff=300, res=0.2, env=0.0, mode=0)[:, 0]
rr = np.sqrt(np.mean(ye[FS // 2:] ** 2)) / np.sqrt(np.mean(yn_[FS // 2:] ** 2))
check('filter: le suiveur d\'enveloppe ouvre le filtre avec le niveau (1500 Hz passe, ≥ +2 dB)', rr > 1.26, f'{20*np.log10(rr):+.1f} dB')
# repliement avec drive modéré
f0 = 9013.7; x = sine(f0, 0.4, FS + 70000)
def alias(y):
    seg = y[FS // 2: FS // 2 + 65536]; w = signal.windows.blackmanharris(len(seg)); X = np.abs(np.fft.rfft(seg * w)); fr = np.fft.rfftfreq(len(seg), 1 / FS)
    mask = (fr > 30) & (fr < 20000); k = 1
    while k * f0 < 20000: mask &= np.abs(fr - k * f0) > 40; k += 1
    return 20 * np.log10(np.sqrt(np.sum(X[mask] ** 2)) / X[np.argmin(np.abs(fr - f0))])
a_f = alias(run('filter', x, cutoff=15000, res=0.0, drive=0.5, mode=0)[:, 0])
print(f'   filter drive 0.5 : parasites inharmoniques {a_f:.1f} dB')
check('filter: parasites de saturation (drive 0.5) < -55 dB', a_f < -55, f'{a_f:.1f} dB')

# ============================ PHASER ============================
print('\n=== PHASER ===')
common('phaser', dict(rate=6.0, depth=1.0, fb=0.9, stages=8, mix=1.0, width=1.0), loud=dict(mix=0.5))
yp = run('phaser', rng.standard_normal(4 * FS) * 0.1, mix=1.0, rate=0.05, depth=0.0, fb=0.0, stages=6, center=800)
Xs = 10 * np.log10(np.abs(np.fft.rfft(yp[FS:3 * FS, 0] * np.hanning(2 * FS))) ** 2 + 1e-20); fr = np.fft.rfftfreq(2 * FS, 1 / FS)
sm = signal.savgol_filter(Xs, 2001, 1); nm = (fr > 200) & (fr < 4000)
depth_notch = np.percentile((Xs - sm)[nm], 3)
print(f'   phaser: profondeur des creux (3e percentile) {depth_notch:.1f} dB')
check('phaser: creux marqués dans le spectre (6 étages, mix 100 %) < -12 dB', depth_notch < -12, f'{depth_notch:.1f} dB')
y = run('phaser', rng.standard_normal(4 * FS) * 0.1, mix=1.0, rate=0.5, depth=1.0, fb=0.5, stages=6)
check('phaser: niveau conservé (±2.5 dB)', abs(db(np.sqrt(np.mean(y[FS:, 0] ** 2))) - db(0.1)) < 2.5)

# ============================ FLANGER ============================
print('\n=== FLANGER ===')
common('flanger', dict(rate=5.0, depth=1.0, manual=6.0, fb=0.9, width=1.0, mix=1.0), loud=dict(mix=0.5))
for fb_ in (0.9, -0.9):
    ys = run('flanger', rng.standard_normal(12 * FS) * 0.2, tail=10, rate=0.1, depth=1.0, fb=fb_, mix=1.0)
    check(f'flanger: stable avec rétroaction {fb_:+.1f} (crête < 12, pas d\'emballement)', np.all(np.isfinite(ys)) and np.max(np.abs(ys)) < 12 and np.sqrt(np.mean(ys[-FS:] ** 2)) < 1e-6)
imp = np.zeros(FS // 2); imp[100] = 1.0
yh = run('flanger', imp, mix=1.0, rate=0.01, depth=0.0, manual=2.0, fb=0.0)[:, 0]
Hf = 20 * np.log10(np.abs(np.fft.rfft(yh, 1 << 16)) + 1e-12); fr = np.fft.rfftfreq(1 << 16, 1 / FS)
sel = (fr > 150) & (fr < 6000); mins = fr[sel][signal.argrelmin(Hf[sel], order=60)[0]]
spacing = np.median(np.diff(mins)) if len(mins) > 3 else float('nan')
check('flanger: peigne espacé de 1/retard (2 ms -> 500 Hz)', abs(spacing - 500) < 30, f'espacement {spacing:.0f} Hz, creux à ' + ' '.join(f'{m:.0f}' for m in mins[:5]))

nf = sum(1 for r in results if not r[1])
print(f'\n{len(results) - nf}/{len(results)} tests OK' + (f'  —  {nf} ECHEC(S)' if nf else ''))
sys.exit(1 if nf else 0)
