#!/usr/bin/env python3
"""fit2.py <cnt.err> <orig.err>... — least-squares per-event costs (pure python)."""
import sys
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
mss = [[float(l.split()[1].rstrip('ms')) for l in open(o) if l.startswith('runner-stats:')] for o in origs]
ms = [min(c) for c in zip(*mss)]
feats = sys.argv[0] and ['in_search', 'in_newest', 'rc_lookups', 'steps']
X = []; Y = []
for r, m in list(zip(rows, ms))[1:]:
    if r['steps'] < 1e5: continue
    cyc = (r['rb_cycles'] + r['gc_cycles'] + r['gm_cycles']) / 2.1
    X.append([r[f] for f in feats]); Y.append(m * 1e6 - cyc)
n = len(feats)
A = [[sum(x[i] * x[j] for x in X) for j in range(n)] for i in range(n)]
b = [sum(x[i] * y for x, y in zip(X, Y)) for i in range(n)]
# gaussian elimination
M = [A[i] + [b[i]] for i in range(n)]
for c in range(n):
    p = max(range(c, n), key=lambda r: abs(M[r][c])); M[c], M[p] = M[p], M[c]
    for r in range(n):
        if r != c:
            f = M[r][c] / M[c][c]
            M[r] = [a - f * bb for a, bb in zip(M[r], M[c])]
coef = [M[i][n] / M[i][i] for i in range(n)]
pred = [sum(c * xi for c, xi in zip(coef, x)) for x in X]
my = sum(Y) / len(Y)
r2 = 1 - sum((y - p) ** 2 for y, p in zip(Y, pred)) / sum((y - my) ** 2 for y in Y)
print('commands', len(X))
for f, c in zip(feats, coef): print(f'  {f:12s} {c:8.1f} ns/event')
print(f'  R^2={r2:.4f}')
tot = [c * sum(x[i] for x in X) for i, c in enumerate(coef)]
print('  share:', ', '.join(f'{f}={100*t/sum(tot):.1f}%' for f, t in zip(feats, tot)))
