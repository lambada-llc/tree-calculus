#!/usr/bin/env python3
"""ownsamp.py <own dump> <samples> <index.tsv> <pid> — sampled self time per owner, overall and per family."""
import sys, struct, collections, re
od, sf, idx, pid = sys.argv[1:5]
names = {}
for line in open(od):
    w = line.split()
    if w[0] == 'O': names[int(w[1])] = ' '.join(w[29:])
fam = {}
for line in open(idx):
    r = line.rstrip('\n').split('\t')
    if r[0] != pid: continue
    fam[int(r[1]) + 1] = re.sub(r'\.\d+$', '', r[5]) or ('compile' if r[6].startswith('reduce string') else 'other')
data = open(sf, 'rb').read(); nl = data.index(b'\n', 6); ml = int(data[6:nl]); recs = data[nl + 1 + ml:]
W = collections.Counter(); F = collections.defaultdict(collections.Counter)
for i in range(0, len(recs), 24):
    rip, ret, ns, ph = struct.unpack_from('<QQII', recs, i)
    o = ph >> 16; p = ph & 0xffff
    W[o] += ns; F[fam.get(p, 'other')][o] += ns
T = sum(W.values())
print(f'sampled cpu {T/1e9:.1f}s')
print('| owner | sampled self % (all) | ' + ' | '.join(f for f, _ in sorted(F.items(), key=lambda kv: -sum(kv[1].values()))[:4]) + ' |')
fams = [f for f, _ in sorted(F.items(), key=lambda kv: -sum(kv[1].values()))[:4]]
print('|---' * (2 + len(fams)) + '|')
for o, w in W.most_common(25):
    print(f'| `{names.get(o, o)[:60]}` | {100*w/T:.1f}% | ' + ' | '.join(f'{100*F[f][o]/max(1,sum(F[f].values())):.1f}%' for f in fams) + ' |')
