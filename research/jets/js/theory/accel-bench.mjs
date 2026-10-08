import * as T from './tc.mjs';
import { makeJet } from './accel.mjs';
import { readFileSync } from 'node:fs';
const B = JSON.parse(readFileSync(new URL('./bench.json', import.meta.url)));
const I = T.ofTernary('21100');
const natList = (xs) => xs.reduceRight((t, n) => T.fork(T.ofNat(n), t), T.LEAF);
const cases = {
  fib: [B.FIB_TERNARY, () => T.ofNat(Number(process.env.FIBN || 16))],
  'silly-exp': [B.SILLY_EXP_TERNARY, () => T.ofNat(Number(process.env.EXPN || 10))],
  'exercise-rules': [B.EXERCISE_RULES_TERNARY, () => T.ofNat(Number(process.env.EXN || 2000))],
  'merge-sort': [B.MERGE_SORT_TERNARY, () => natList(Array.from({ length: Number(process.env.MSN || 100) }, (_, i) => Number(process.env.MSN || 100) - i))],
  size: [B.SIZE_TERNARY, () => T.ofTernary(B.SIZE_TERNARY)],
};
const only = process.argv[2];
for (const [name, [prog, mkArg]] of Object.entries(cases)) {
  if (only && name !== only) continue;
  const f = T.apply(I, T.ofTernary(prog));
  const x = mkArg();
  const runs = [];
  const base = T.makeStats();
  const r0 = T.apply(f, x, {}, base);
  const row = (label, st, extra = '') => console.log(`${name.padEnd(15)} ${label.padEnd(26)} steps ${String(st.steps).padStart(10)}  ×${(base.steps / st.steps).toFixed(2).padStart(6)}  jets ${String(st.jets).padStart(8)} saved ${String(st.jetSaved).padStart(9)} ${extra}`);
  row('plain', base);
  for (const depth of [0, 1, 2, 4]) {
    for (const fuel of [1000]) {
      const stats = { rules: 0, residualNodes: 0 };
      const st = T.makeStats();
      const r = T.apply(f, x, { jet: makeJet({ fuel, depth, stats }) }, st);
      if (r !== r0) throw new Error(`${name}: jet result differs!`);
      if (st.steps + st.jetSaved !== base.steps) console.log('  (step identity off:', st.steps + st.jetSaved, base.steps, ')');
      row(`head-rules depth=${depth}`, st, `rules ${stats.rules}, residual nodes built ${stats.residualNodes}`);
    }
  }
  const sm = T.makeStats();
  const rm = T.apply(f, x, { memo: new Map() }, sm);
  if (rm !== r0) throw new Error('memo differs');
  row('memo (all recursing)', sm, `hits ${sm.memoHits}`);
}
