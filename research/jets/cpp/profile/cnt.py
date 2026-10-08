#!/usr/bin/env python3
"""cnt.py <cnt.err> <index.tsv> <pid> [orig.err] — per-family event rates from cumulative CNT lines."""
import sys, re, collections
errf, idx, pid = sys.argv[1:4]
fam = {}
for line in open(idx):
    r = line.rstrip('\n').split('\t')
    if r[0] != pid: continue
    n = re.sub(r'\.\d+$', '', r[5]) or ('compile' if r[6].startswith('reduce string') else 'other')
    fam[int(r[1])] = n
ms = []
if len(sys.argv) > 4:
    for line in open(sys.argv[4]):
        if line.startswith('runner-stats:'): ms.append(float(line.split()[1].rstrip('ms')))
prev = None; seq = -1; agg = collections.defaultdict(collections.Counter)
for line in open(errf):
    if not line.startswith('CNT '): continue
    seq += 1
    kv = {}
    for w in line.split():
        if '=' in w:
            k, v = w.split('=', 1)
            try: kv[k] = float(v)
            except: pass
    if prev is None: prev = {k: 0.0 for k in kv}
    d = {k: kv[k] - prev.get(k, 0) for k in kv if k not in ('in_load_avg', 'interned', 'mask', 'head', 'memo_slots', 'rb_cap_max')}
    # load factor average: weight by searches
    d['load_x_search'] = kv['in_load_avg'] * kv['in_search'] - prev.get('in_load_avg', 0) * prev.get('in_search', 0)
    if seq < len(ms): d['ms'] = ms[seq]
    f = fam.get(seq, 'other')
    for k, v in d.items(): agg[f][k] += v; agg['ALL'][k] += v
    prev = kv
order = sorted(agg, key=lambda f: -agg[f]['steps'])
def row(name, fn):
    print(f'| {name} | ' + ' | '.join(fn(agg[f]) for f in order[:8]) + ' |')
print('| metric | ' + ' | '.join(order[:8]) + ' |'); print('|---' * 9 + '|')
row('steps (M)', lambda a: f"{a['steps']/1e6:.1f}")
row('ms (pristine)', lambda a: f"{a['ms']:.0f}")
row('ns/step', lambda a: f"{1e6*a['ms']/max(1,a['steps']):.1f}")
for k in ['leaf', 'stem', 'K', 'S', 'TL', 'TS', 'TF']:
    row(f'{k} steps %', lambda a, k=k: f"{100*a[k]/max(1,a['steps']):.1f}")
row('interns / step', lambda a: f"{a['in_calls']/max(1,a['steps']):.3f}")
row('  newest fast path % of interns', lambda a: f"{100*a['in_newest']/max(1,a['in_calls']):.1f}")
row('  search % of interns', lambda a: f"{100*a['in_search']/max(1,a['in_calls']):.1f}")
row('  hash-cons hit (existing) % of interns', lambda a: f"{100*a['in_hit']/max(1,a['in_calls']):.1f}")
row('  hit % of searches', lambda a: f"{100*a['in_hit']/max(1,a['in_search']):.1f}")
row('  probes / search', lambda a: f"{a['in_probes']/max(1,a['in_search']):.2f}")
row('  mean load factor at search', lambda a: f"{a['load_x_search']/max(1,a['in_search']):.3f}")
row('fresh nodes / step', lambda a: f"{(a['in_newest']+a['in_fresh'])/max(1,a['steps']):.3f}")
row('  from free list %', lambda a: f"{100*a['in_free']/max(1,a['in_free']+a['in_head']):.1f}")
row('recall / step', lambda a: f"{a['rc_calls']/max(1,a['steps']):.3f}")
row('  cold-skipped %', lambda a: f"{100*a['rc_coldskip']/max(1,a['rc_calls']):.1f}")
row('  memo probes / step', lambda a: f"{a['rc_lookups']/max(1,a['steps']):.3f}")
row('  hit % of probes', lambda a: f"{100*a['rc_hits']/max(1,a['rc_lookups']):.1f}")
row('memo puts / step', lambda a: f"{a['mp_puts']/max(1,a['steps']):.3f}")
row('  evicting % of puts', lambda a: f"{100*a['mp_evict']/max(1,a['mp_puts']):.1f}")
row('  MEMOIZE pops below MIN_STEPS / step', lambda a: f"{a['mp_skip_min']/max(1,a['steps']):.3f}")
row('frames pushed / step', lambda a: f"{(a['fr_apply_to']+a['fr_caa']+a['fr_memo'])/max(1,a['steps']):.3f}")
row('rebuild_interned calls (growth/gc)', lambda a: f"{a['rb_growth']:.0f}/{a['rb_gc']:.0f}")
row('rebuild Mcycles', lambda a: f"{a['rb_cycles']/1e6:.0f}")
row('rebuild nodes reinserted (M)', lambda a: f"{a['rb_nodes']/1e6:.1f}")
row('GC calls / Mcycles', lambda a: f"{a['gc_calls']:.0f} / {a['gc_cycles']/1e6:.0f}")
row('grow_memo calls / Mcycles', lambda a: f"{a['gm_calls']:.0f} / {a['gm_cycles']/1e6:.0f}")
