#!/usr/bin/env python3
"""fit.py <cnt.err> <orig.err>... — least-squares per-event costs from per-command deltas."""
import sys
from lsq import np
cnt = sys.argv[1]; origs = sys.argv[2:]
rows = []; prev = None
for line in open(cnt):
    if not line.startswith('CNT '): continue
    kv = {}
    for w in line.split():
        if '=' in w:
            k, v = w.split('=', 1)
            try: kv[k] = float(v)
            except: pass
    if prev is None: prev = {k: 0.0 for k in kv}
    rows.append({k: kv[k] - prev.get(k, 0) for k in kv}); prev = kv
mss = []
for o in origs:
    ms = [float(l.split()[1].rstrip('ms')) for l in open(o) if l.startswith('runner-stats:')]
    mss.append(ms)
ms = np.min(np.array(mss), axis=0)  # best of runs per command
feats = ['in_search', 'in_newest', 'rc_lookups', 'steps', 'rb_cycles', 'gc_cycles', 'gm_cycles']
X = np.array([[r[f] for f in feats] for r in rows[1:]]); y = ms[1:] * 1e6  # ns
# convert cycles to ns at 2.1 GHz and fix their coefficient to 1 (subtract)
cyc = (X[:, 4] + X[:, 5] - 0 + X[:, 6]) / 2.1
y2 = y - cyc
X2 = X[:, :4]
sel = X2[:, 3] > 1e5
coef, res, rk, sv = np.linalg.lstsq(X2[sel], y2[sel], rcond=None)
pred = X2[sel] @ coef
print('commands used', sel.sum(), 'of', len(sel))
for f, c in zip(feats, coef): print(f'  {f:12s} {c:8.1f} ns')
print('  R^2 = %.4f' % (1 - ((y2[sel] - pred) ** 2).sum() / ((y2[sel] - y2[sel].mean()) ** 2).sum()))
tot = X2[sel].sum(axis=0) * coef
print('  share of fitted time:', ', '.join(f'{f}={100*t/tot.sum():.1f}%' for f, t in zip(feats, tot)))
print('  rebuild+gc+growmemo time (rdtsc) %.2f s of %.2f s' % (cyc.sum() / 1e9, y.sum() / 1e9))
