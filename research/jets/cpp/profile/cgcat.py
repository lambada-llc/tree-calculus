#!/usr/bin/env python3
"""cgcat.py <cachegrind.out> — per (function, file:line) event totals, categorized like pie.py."""
import sys, collections, re, os
ev = None; fl = fi = fn = None
tot = collections.defaultdict(lambda: [0] * 9)
for line in open(sys.argv[1]):
    if line.startswith('events:'): ev = line.split()[1:]; continue
    if line.startswith('fl='): fl = line[3:].strip(); fi = None; continue
    if line.startswith('fi=') or line.startswith('fe='): fi = line[3:].strip(); continue
    if line.startswith('fn='): fn = line[3:].strip(); fi = None; continue
    if not line[:1].isdigit(): continue
    p = line.split(); ln = int(p[0]); vals = list(map(int, p[1:])) + [0] * (9 - len(p) + 1)
    f = os.path.basename(fi or fl or '?')
    k = (fn, f, ln)
    t = tot[k]
    for i, v in enumerate(vals[:9]): t[i] += v
H = 'eager-graph-nil-mmap-32.hpp'
def cat(fn, f, ln):
    if 'rebuild_interned' in fn or 'end_run' in fn: return 'rebuild_interned (table re-lay)'
    if re.search(r'::(collect|mark|sweep)\(', fn): return 'GC (mark/sweep/memo filter)'
    if 'grow_memo' in fn or 'size_memo' in fn: return 'memo grow/clear'
    if 'apply' not in fn: return 'other (parse/marshal/IO/libc)'
    if f == H:
        if 313 <= ln <= 319: return 'intern: newest fast path (insert)'
        if 300 <= ln <= 310: return 'intern: alloc (node write)'
        if 293 <= ln <= 297: return 'hash (attributed to caller below)'
        if 340 <= ln <= 352 or 381 <= ln <= 415 or 359 <= ln <= 372: return 'intern: search (slot probe)'
        if 417 <= ln <= 421 or 465 <= ln <= 472: return 'memo lookup (recall)'
        if 473 <= ln <= 474: return 'frames (push/pop/dispatch loop)'
        if 425 <= ln <= 429 or ln in (902, 903): return 'memo put'
        if 160 <= ln <= 180 or 891 <= ln <= 918: return 'frames (push/pop/dispatch loop)'
        if 828 <= ln <= 889 or 612 <= ln <= 625 or ln in (741, 742, 748, 749): return 'rule dispatch (node loads, branches)'
        return f'other hpp:{ln}'
    if f.startswith('stl_') or f in ('vector.tcc', 'new_allocator.h', 'alloc_traits.h'): return 'frames (push/pop/dispatch loop)'
    return f'other {f}:{ln}'
agg = collections.defaultdict(lambda: [0] * 9)
for (fn, f, ln), v in tot.items():
    c = cat(fn, f, ln)
    for i in range(9): agg[c][i] += v[i]
names = ev
T = [sum(v[i] for v in agg.values()) for i in range(9)]
idx = {n: i for i, n in enumerate(names)}
print('| activity | Ir | Ir % | D1 read miss | DLL read miss | DLL write miss | est. cycles % (Ir + 12*D1mr + 200*DLmr) |')
print('|---|---|---|---|---|---|---|')
def est(v): return v[idx['Ir']] + 12 * (v[idx['D1mr']]) + 200 * v[idx['DLmr']] + 50 * v[idx['DLmw']]
E = sum(est(v) for v in agg.values())
for c, v in sorted(agg.items(), key=lambda kv: -est(kv[1])):
    print(f"| {c} | {v[idx['Ir']]:,} | {100*v[idx['Ir']]/T[idx['Ir']]:.1f}% | {v[idx['D1mr']]:,} | {v[idx['DLmr']]:,} | {v[idx['DLmw']]:,} | {100*est(v)/E:.1f}% |")
print(f"| total | {T[idx['Ir']]:,} | | {T[idx['D1mr']]:,} | {T[idx['DLmr']]:,} | {T[idx['DLmw']]:,} | |")
