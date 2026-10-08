#!/usr/bin/env python3
"""pie.py <binary> <sample file> <index.tsv> <pid> — activity pie, overall and per test family.
Reuses samp.py's symbolization (via addr2line) on per-sample keys."""
import sys, struct, subprocess, collections, os, re
exe, sf, idx, pid = sys.argv[1:5]
data = open(sf, 'rb').read()
nl = data.index(b'\n', 6); ml = int(data[6:nl]); maps = data[nl+1:nl+1+ml].decode(); recs = data[nl+1+ml:]
segs = []
for line in maps.splitlines():
    p = line.split()
    if len(p) < 6 or 'x' not in p[1]: continue
    lo, hi = (int(x, 16) for x in p[0].split('-')); segs.append((lo, hi, p[5]))
bn = os.path.basename(exe)
base = min(int(l.split()[0].split('-')[0], 16) for l in maps.splitlines() if l.split()[-1].endswith(bn) and int(l.split()[2], 16) == 0)
def path_of(a):
    for lo, hi, p in segs:
        if lo <= a < hi: return p
    return None
# family per phase (phase = seq + 1)
fam = {}; steps = {}
for line in open(idx):
    r = line.rstrip('\n').split('\t')
    if r[0] != pid: continue
    n = r[5]; n = re.sub(r'\.\d+$', '', n) or ('compile' if r[6].startswith('reduce string') else 'other')
    fam[int(r[1]) + 1] = n; steps[int(r[1]) + 1] = int(r[2])
keys = collections.Counter(); per = collections.defaultdict(collections.Counter)
for i in range(0, len(recs), 24):
    rip, ret, ns, ph = struct.unpack_from('<QQII', recs, i)
    p = path_of(rip)
    if p and p.endswith(bn): k = ('exe', rip - base)
    else:
        rp = path_of(ret)
        k = ('lib', (ret - base) if rp and rp.endswith(bn) else -1)
    keys[k] += ns; per[fam.get(ph, 'other')][k] += ns
addrs = sorted({k[1] for k in keys if k[1] >= 0})
p = subprocess.run(['addr2line', '-a', '-i', '-f', '-C', '-e', exe], input='\n'.join(hex(a) for a in addrs) + '\n', capture_output=True, text=True)
info = {}; cur = None; L = p.stdout.splitlines(); i = 0
while i < len(L):
    if L[i].startswith('0x'): cur = int(L[i], 16); info[cur] = []; i += 1; continue
    info[cur].append((L[i], os.path.basename(L[i+1].split(' ')[0]))); i += 2
H = 'eager-graph-nil-mmap-32.hpp'
def fnames(a):
    return [re.sub(r'^.*BasicEagerGraphNilMmap32<(?:true|false)>::', '', f).split('(')[0] + '@' + l for f, l in info.get(a, [])]
def classify(k):
    kind, a = k
    ch = fnames(a) if a >= 0 else []
    s = ' < '.join(ch)
    if kind == 'lib':
        if 'rebuild_interned' in s: return 'rebuild_interned (table re-lay)'
        if 'grow_memo' in s or 'size_memo' in s: return 'memo grow/clear'
        if 'collect' in s or 'sweep' in s: return 'GC (mark/sweep/memo filter)'
        return 'other (parse/marshal/IO/libc)'
    if 'rebuild_interned' in s or 'end_run' in s: return 'rebuild_interned (table re-lay)'
    if re.search(r'\b(collect|mark|sweep|marked)@', s) and 'collect_if_over_budget' not in s.split(' < ')[0]: return 'GC (mark/sweep/memo filter)'
    if 'grow_memo' in s or 'size_memo' in s: return 'memo grow/clear'
    inner = ch[0] if ch else '?'
    if 'apply@' not in s and 'intern' not in s and 'recall' not in s: return 'other (parse/marshal/IO/libc)'
    if 'insert_interned' in s and 'intern@' + H + ':403' in s.replace(':403', ':403'): return 'intern: newest fast path (insert)'
    if inner.startswith('insert_interned'): return 'intern: newest fast path (insert)'
    if inner.startswith('alloc@'): return 'intern: alloc (node write)'
    if inner.startswith('reserve_interned'): return 'intern: search (slot probe)'
    if inner.startswith('slot@') or inner.startswith('in_run@') or inner.startswith('intern@') or inner.startswith('find@') or (inner.startswith('hash@') and ('slot@' in s or 'insert_interned' in s)):
        if 'insert_interned' in s: return 'intern: newest fast path (insert)'
        return 'intern: search (slot probe)'
    if 'memo_put' in s: return 'memo put'
    if 'recall@' in s or 'memo_get@' in s:
        if 'emplace_back' in s or '@' + H + ':473' in s: return 'frames (push/pop/dispatch loop)'
        return 'memo lookup (recall)'
    if 'collect_if_over_budget' in s: return 'rule dispatch (node loads, branches)'
    if 'Frame' in s or 'emplace_back' in s or 'pop_back' in s or 'size@stl_vector' in s or 'vector.tcc' in s: return 'frames (push/pop/dispatch loop)'
    m = re.match(r'apply@' + re.escape(H) + r':(\d+)', inner)
    if m:
        ln = int(m.group(1))
        if 894 <= ln <= 918: return 'frames (push/pop/dispatch loop)'
        if ln in (902, 903): return 'memo put'
        return 'rule dispatch (node loads, branches)'
    return 'unclassified:' + inner
def pie(c):
    agg = collections.Counter()
    for k, w in c.items(): agg[classify(k)] += w
    return agg
cats = ['intern: search (slot probe)', 'intern: newest fast path (insert)', 'intern: alloc (node write)', 'rebuild_interned (table re-lay)', 'memo lookup (recall)', 'memo put', 'memo grow/clear', 'frames (push/pop/dispatch loop)', 'rule dispatch (node loads, branches)', 'GC (mark/sweep/memo filter)', 'other (parse/marshal/IO/libc)']
tot = pie(keys); T = sum(tot.values())
fams = sorted(per, key=lambda f: -sum(per[f].values()))
fs = collections.Counter()
for ph, f in fam.items(): fs[f] += steps[ph]
print('| activity | all (%.1f s cpu, %.0f ns/step) | ' % (T / 1e9, T / max(1, sum(fs.values()))) + ' | '.join(f'{f} ({sum(per[f].values())/1e9:.1f}s, {sum(per[f].values())/max(1,fs[f]):.0f} ns/step)' for f in fams[:6]) + ' |')
print('|---' * (2 + min(6, len(fams))) + '|')
pf = {f: pie(per[f]) for f in fams[:6]}
for c in cats + sorted(k for k in tot if k not in cats):
    print(f'| {c} | {100*tot[c]/T:.1f}% | ' + ' | '.join(f'{100*pf[f][c]/max(1,sum(pf[f].values())):.1f}%' for f in fams[:6]) + ' |')
