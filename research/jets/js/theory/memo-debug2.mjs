import * as T from './tc.mjs';
import { readFileSync } from 'node:fs';
const B = JSON.parse(readFileSync(new URL('./bench.json', import.meta.url)));
const I = T.ofTernary('21100');
const f = T.apply(I, T.ofTernary(B.FIB_TERNARY));
for (const n of [8, 10, 12, 14]) {
  const pairs = new Set(); let steps = 0;
  T.apply(f, T.ofNat(n), { onReduce: (a, b) => { steps++; pairs.add(a * 67108864 + b); } });
  const st = T.makeStats();
  T.apply(f, T.ofNat(n), { memo: new Map() }, st);
  console.log(n, 'steps', steps, 'distinct (a,b)', pairs.size, 'memo steps', st.steps, 'hits', st.memoHits);
}
