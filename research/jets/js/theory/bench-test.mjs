import * as T from './tc.mjs';
import { readFileSync } from 'node:fs';
const B = JSON.parse(readFileSync(new URL('./bench.json', import.meta.url)));
const I = T.ofTernary('21100'); // identity △(△(△△))△ ? check
console.log('I', T.show(I));
const fib = T.ofTernary(B.FIB_TERNARY);
for (const memo of [false, true]) {
  const st = T.makeStats();
  const f = T.apply(I, fib, {}, st);
  const t0 = Date.now();
  const r = T.apply(f, T.ofNat(Number(process.argv[2] || 20)), memo ? { memo: new Map() } : {}, st);
  console.log('memo', memo, 'fib ->', T.toNat(r), st, Date.now() - t0, 'ms');
}
