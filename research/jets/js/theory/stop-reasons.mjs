// Where do maximal parametric-argument rules stop? (per distinct S-head, weighted by applications)
import * as T from './tc.mjs';
import * as S from './sym.mjs';
import { readFileSync } from 'node:fs';
const env = T.loadBundle('/home/user/lambada/compiler/compile_file.dag');
const compile = env.get('Lambada.compile_file');
const src = readFileSync('/home/user/arboretum/src/core.lamb', 'utf8');
const str = [...Buffer.from(src, 'utf8')].reduceRight((t, c) => T.fork(T.ofNat(c), t), T.LEAF);
const heads = new Map();
T.apply(compile, str, { onReduce: (a) => heads.set(a, (heads.get(a) || 0) + 1) });
const byStop = {}; const lens = [];
let apps = 0;
for (const [a, n] of heads) {
  apps += n;
  const tr = S.derive(S.reduce(S.lit(a), S.svar(0)), { fuel: 100000 });
  const key = tr.stop;
  byStop[key] = byStop[key] || { heads: 0, apps: 0, steps: 0 };
  byStop[key].heads++; byStop[key].apps += n; byStop[key].steps += n * tr.steps;
  lens.push([tr.steps, n]);
}
console.log('distinct heads', heads.size, 'applications', apps);
for (const [k, v] of Object.entries(byStop)) console.log(k.padEnd(6), `heads ${v.heads}`, `apps ${v.apps} (${(100 * v.apps / apps).toFixed(1)}%)`, `mean rule length (reduce steps) ${(v.steps / v.apps).toFixed(1)}`);
lens.sort((x, y) => x[0] - y[0]);
const q = (p) => { let acc = 0; for (const [l, n] of lens) { acc += n; if (acc >= p * apps) return l; } };
console.log('application-weighted rule length quantiles p50/p90/p99/max:', q(0.5), q(0.9), q(0.99), lens[lens.length - 1][0]);
