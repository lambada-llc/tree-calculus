#!/usr/bin/env python3
"""own.py <own dump> [top=40] [cost ns: step,search,newest,probe,alloc] — owner table.
Modeled time = events x per-event cost (default costs from the sampler pie on p1010)."""
import sys
f = sys.argv[1]; top = int(sys.argv[2]) if len(sys.argv) > 2 else 40
C = list(map(float, sys.argv[3].split(','))) if len(sys.argv) > 3 else [4.6, 133, 91, 75, 11]
rows = []; ev = None; L = []
for line in open(f):
    w = line.split()
    if line.startswith('#own'): ev = {k: int(v) for k, v in (x.split('=') for x in w[2:])}; continue
    if w[0] == 'O':
        i = int(w[1]); tr = int(w[2]); nums = list(map(int, w[3:3 + 8 + 5 + 5 + 8])); name = ' '.join(w[3 + 26:])
        kind = nums[:8]; self_ = nums[8:13]; incl = nums[13:18]; sh, mh, act, born, dead, dnc, doa, dmemo = nums[18:26]
        rows.append(dict(i=i, tr=tr, kind=kind, self=self_, incl=incl, sh=sh, mh=mh, act=act, born=born, dead=dead, dnc=dnc, doa=doa, dmemo=dmemo, name=name))
    elif w[0] == 'L': L.append(list(map(int, w[1:])))
def mt(v): return sum(a * b for a, b in zip(v, C))
TOT = mt([ev['steps'], ev['search'], ev['newest'], ev['probe'], ev['alloc']])
print(f"total events: steps={ev['steps']:,} searches={ev['search']:,} newest={ev['newest']:,} probes={ev['probe']:,} allocs={ev['alloc']:,}; modeled {TOT/1e9:.1f} s")
def short(n): return n if len(n) < 70 else n[:67] + '...'
print('\n### by inclusive modeled time\n')
print('| # | owner | incl time | incl steps | incl allocs | self time | self steps | activations | self steps: K/S/build/triage |')
print('|---|---|---|---|---|---|---|---|---|')
for r in sorted(rows, key=lambda r: -mt(r['incl']))[:top]:
    k = r['kind']; s = max(1, sum(k))
    print(f"| {r['i']} | `{short(r['name'])}` | {100*mt(r['incl'])/TOT:.1f}% | {r['incl'][0]/1e6:.1f}M | {r['incl'][4]/1e6:.1f}M | {100*mt(r['self'])/TOT:.1f}% | {r['self'][0]/1e6:.1f}M | {r['act']:,} | {100*k[2]/s:.0f}/{100*k[3]/s:.0f}/{100*(k[0]+k[1])/s:.0f}/{100*(k[4]+k[5]+k[6])/s:.0f} |")
print('\n### by self modeled time\n')
print('| # | owner | self time | self steps | searches (hit%) | newest inserts | memo probes (hit%) | allocs | built: dead % / dead never-a-child % |')
print('|---|---|---|---|---|---|---|---|---|')
for r in sorted(rows, key=lambda r: -mt(r['self']))[:top]:
    s = r['self']
    print(f"| {r['i']} | `{short(r['name'])}` | {100*mt(s)/TOT:.1f}% | {s[0]/1e6:.1f}M | {s[1]/1e6:.1f}M ({100*r['sh']/max(1,s[1]):.0f}%) | {s[2]/1e6:.1f}M | {s[3]/1e6:.1f}M ({100*r['mh']/max(1,s[3]):.0f}%) | {s[4]/1e6:.1f}M | {100*r['dead']/max(1,r['born']):.1f}% / {100*r['dnc']/max(1,r['born']):.1f}% |")
if L:
    cls = ['stem △x', 'K-closure △△y', 'S-closure B-shape △(△(K p))y', 'S-closure C-shape △(△x)(K q)', 'S-closure other', 'triage closure △(△wx)y', 'parse/marshal', '?']
    print('\n### lifetime of nodes built during reduction (end-of-command reachability)\n')
    tot = [0, 0, 0]
    for c, u, a, m, d in L: tot[0] += a; tot[1] += m; tot[2] += d
    allb = sum(tot)
    print(f'born {allb:,}: reachable from roots/result {100*tot[0]/allb:.2f}%, only from memo {100*tot[1]/allb:.2f}%, unreachable {100*tot[2]/allb:.2f}%\n')
    print('| class | born | % of born | reachable | memo-only | dead | dead: used as fn only | as arg only | fn+arg, no child | became a child | memo key/val |')
    print('|---|---|---|---|---|---|---|---|---|---|---|')
    for ci, cn in enumerate(cls):
        rs = [x for x in L if x[0] == ci]
        if not rs: continue
        b = sum(x[2] + x[3] + x[4] for x in rs); dead = sum(x[4] + x[3] for x in rs)
        def dd(pred): return sum(x[4] + x[3] for x in rs if pred(x[1]))
        print(f"| {cn} | {b:,} | {100*b/allb:.1f}% | {100*sum(x[2] for x in rs)/b:.2f}% | {100*sum(x[3] for x in rs)/b:.2f}% | {100*sum(x[4] for x in rs)/b:.2f}% | {100*dd(lambda u: u & 7 == 1)/b:.1f}% | {100*dd(lambda u: u & 7 == 2)/b:.1f}% | {100*dd(lambda u: u & 7 == 3)/b:.1f}% | {100*dd(lambda u: bool(u & 4))/b:.1f}% | {100*dd(lambda u: bool(u & 8))/b:.1f}% |")
    dn = sum(x[4] + x[3] for x in L if not (x[1] & 4)); print(f'\ndead and never a child of another node: {100*dn/allb:.1f}% of born; never used at all: {100*sum(x[4]+x[3] for x in L if (x[1]&15)==0)/allb:.1f}%')
