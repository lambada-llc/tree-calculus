import * as T from './tc.mjs';
import { readFileSync } from 'node:fs';
const B = JSON.parse(readFileSync(new URL('./bench.json', import.meta.url)));
const I = T.ofTernary('21100');
const f = T.apply(I, T.ofTernary(B.FIB_TERNARY));
console.log('fib program DAG size', T.size(f), T.show(f, 3));
// which (a,b) pairs with b a small nat repeat most?
const pairs = new Map();
T.apply(f, T.ofNat(12), { onReduce: (a, b) => { const n = T.toNat(b); if (!Number.isNaN(n) && n <= 12 && T.isFork(a) && T.isStem(T.L[a])) { const k = a + ':' + n; pairs.set(k, (pairs.get(k) || 0) + 1); } } });
const top = [...pairs.entries()].sort((x, y) => y[1] - x[1]).slice(0, 12);
console.log(top);
