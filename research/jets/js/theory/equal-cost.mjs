import * as T from './tc.mjs';
const env = T.loadBundle('/home/user/forest/src/.dag-bundle-arboretum-reduced');
const eq = env.get('equal');
const natList = (xs) => xs.reduceRight((t, n) => T.fork(T.ofNat(n), t), T.LEAF);
for (const n of [10, 100, 1000, 10000]) {
  const a = natList(Array.from({ length: n }, (_, i) => i)), b = natList(Array.from({ length: n }, (_, i) => i));
  const c = natList(Array.from({ length: n }, (_, i) => (i === n - 1 ? 7 : i)));
  const s1 = T.makeStats(), s2 = T.makeStats(), s3 = T.makeStats();
  const r1 = T.apply(T.apply(eq, a), b, {}, s1);
  const r2 = T.apply(T.apply(eq, a), c, {}, s2);
  const r3 = T.apply(T.apply(eq, a), b, { memo: new Map() }, s3);
  console.log(`n=${n}: same index ${a === b}; equal a a -> ${T.show(r1, 2)} in ${s1.steps} steps (memo: ${s3.steps}); equal a a' (last differs) -> ${T.show(r2, 2)} in ${s2.steps} steps; DAG size ${T.size(a)}`);
}
