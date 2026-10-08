// How many distinct *patterns* (heads generalized to what their rule's run inspects) cover the
// distinct heads that ground-head rules need — i.e. how much holes in P compress the rule set.
import * as T from './tc.mjs';
import * as S from './sym.mjs';
import { readFileSync } from 'node:fs';
const B = JSON.parse(readFileSync(new URL('./bench.json', import.meta.url)));
const I = T.ofTernary('21100');
const natList = (xs) => xs.reduceRight((t, n) => T.fork(T.ofNat(n), t), T.LEAF);
const N = Number(process.env.MSN || 100);
const cases = {
  fib: [B.FIB_TERNARY, () => T.ofNat(16)],
  'silly-exp': [B.SILLY_EXP_TERNARY, () => T.ofNat(10)],
  'exercise-rules': [B.EXERCISE_RULES_TERNARY, () => T.ofNat(2000)],
  'merge-sort': [B.MERGE_SORT_TERNARY, () => natList(Array.from({ length: N }, (_, i) => N - i))],
  size: [B.SIZE_TERNARY, () => T.ofTernary(B.SIZE_TERNARY)],
};
const depth = Number(process.env.DEPTH || 0);
const policy = (o) => o.where === 'arg';
for (const [name, [prog, mkArg]] of Object.entries(cases)) {
  const f = T.apply(I, T.ofTernary(prog));
  const x = mkArg();
  const heads = new Map(); // head -> count
  T.apply(f, x, { onReduce: (a) => heads.set(a, (heads.get(a) || 0) + 1) });
  const pats = new Map(); // key -> {heads, apps, steps, size}
  let checked = 0, bad = 0, useful = 0, usefulApps = 0;
  for (const [a, n] of heads) {
    const viewed = new Set();
    S.hooks.onView = (v) => viewed.add(v);
    const tr = S.derive(S.reduce(S.lit(a), S.svar(0)), { fuel: 1000, policy, maxSplits: depth });
    S.hooks.onView = null;
    if (tr.t === 'rule' && tr.steps < 2) continue;
    useful++; usefulApps += n;
    const P = S.generalize(a, viewed, { i: 1 });
    const key = S.patKey(P);
    const e = pats.get(key) || { heads: 0, apps: 0, size: S.patSize(P), holes: S.maxVar(P) };
    e.heads++; e.apps += n; pats.set(key, e);
    // re-derive over the pattern: must take the same transitions without splitting on a hole
    if (checked < 400 && tr.t === 'rule') {
      checked++;
      const tr2 = S.derive(S.reduce(P, S.svar(0)), { fuel: 1000, policy, maxSplits: depth });
      if (!(tr2.t === 'rule' && tr2.steps === tr.steps && tr2.stop === tr.stop)) bad++;
    }
  }
  const sizes = [...pats.values()].map((e) => e.size);
  const avg = (xs) => (xs.reduce((s, v) => s + v, 0) / xs.length).toFixed(1);
  const fullSizes = [...heads.keys()].map((a) => T.size(a));
  console.log(`${name.padEnd(15)} distinct heads ${String(heads.size).padStart(6)} (avg DAG size ${avg(fullSizes)}), with a rule (>=2 steps) ${String(useful).padStart(6)}; ` +
    `distinct patterns ${String(pats.size).padStart(5)} (avg checked nodes ${avg(sizes)}, max holes ${Math.max(...[...pats.values()].map((e) => e.holes))}); re-derivation checks ${checked}, mismatches ${bad}`);
}
