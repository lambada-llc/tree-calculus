#!/usr/bin/env python3
"""samp.py <binary> <sample file> [phase-lo phase-hi] -> per-location table (cpu-time weighted).
Writes <sample>.loc.tsv: weight_ns  samples  object  func  innermost file:line  inline-chain"""
import sys, struct, subprocess, collections, os, bisect
exe, sf = sys.argv[1:3]
plo, phi = (int(sys.argv[3]), int(sys.argv[4])) if len(sys.argv) > 4 else (0, 1 << 32)
data = open(sf, 'rb').read()
assert data.startswith(b'SAMP1\n')
nl = data.index(b'\n', 6); ml = int(data[6:nl]); maps = data[nl+1:nl+1+ml].decode(); recs = data[nl+1+ml:]
segs = []
for line in maps.splitlines():
    p = line.split()
    if len(p) < 6 or 'x' not in p[1]: continue
    lo, hi = (int(x, 16) for x in p[0].split('-'))
    segs.append((lo, hi, int(p[2], 16), p[5]))
segs.sort()
# first mapping of the exe gives the load base (PIE)
base = min(int(l.split()[0].split('-')[0], 16) for l in maps.splitlines() if l.split()[-1].endswith(os.path.basename(exe)) and int(l.split()[2],16) == 0)
def locate(a):
    for lo, hi, off, path in segs:
        if lo <= a < hi: return path, lo, off
    return None, 0, 0
W = collections.Counter(); N = collections.Counter(); tot = 0; totn = 0
for i in range(0, len(recs), 24):
    rip, ret, ns, ph = struct.unpack_from('<QQII', recs, i)
    if not (plo <= ph <= phi): continue
    path, lo, off = locate(rip)
    if path and path.endswith(os.path.basename(exe)):
        key = ('exe', rip - base)
    else:
        rp, rlo, roff = locate(ret)
        key = ((os.path.basename(path) if path else '?'), (ret - base) if (rp and rp.endswith(os.path.basename(exe))) else -1)
    W[key] += ns; N[key] += 1; tot += ns; totn += 1
addrs = sorted({k[1] for k in W if k[1] >= 0})
info = {}
if addrs:
    p = subprocess.run(['addr2line', '-i', '-f', '-C', '-e', exe], input='\n'.join(hex(a) for a in addrs) + '\n' + hex(0) + '\n', capture_output=True, text=True)
    # with -i, number of lines per address varies; use a sentinel: rerun with addresses one by one would be slow; use -a to print address markers
    p = subprocess.run(['addr2line', '-a', '-i', '-f', '-C', '-e', exe], input='\n'.join(hex(a) for a in addrs) + '\n', capture_output=True, text=True)
    cur = None
    lines = p.stdout.splitlines()
    i = 0
    while i < len(lines):
        if lines[i].startswith('0x'):
            cur = int(lines[i], 16); info[cur] = []; i += 1; continue
        fn = lines[i]; fl = lines[i+1] if i + 1 < len(lines) else '?'
        info[cur].append((fn, os.path.basename(fl.split(' ')[0])))
        i += 2
with open(sf + '.loc.tsv', 'w') as f:
    for k, w in W.most_common():
        obj, a = k
        chain = info.get(a, [('?', '?')])
        inner = chain[0]
        f.write(f"{w}\t{N[k]}\t{obj}\t{hex(a) if a>=0 else '-'}\t{inner[0][:80]}\t{inner[1]}\t{' < '.join(c[0].split('(')[0][-40:]+'@'+c[1] for c in chain)}\n")
print(f'samples={totn} cpu_s={tot/1e9:.2f}')
