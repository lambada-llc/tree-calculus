import * as T from './tc.mjs';
import * as S from './sym.mjs';
import { trace } from './trace.mjs';
const env = T.loadBundle('/home/user/forest/src/.dag-bundle-arboretum-reduced');
const names = (i) => ['f', 'x'][i] ?? 'v' + i;
for (const n of ['fix', 'fix__wait']) {
  const F = env.get(n);
  const P = S.derive(S.reduce(S.lit(F), S.svar(0))).s.r;
  console.log('==', n);
  trace(S.reduce(P, S.svar(1)), names);
}
// concrete: allocation of one unfolding, with f = some closure and x = some value, fresh arena state
const F = env.get('fix');
const f = env.get('Nat.add'), x = T.ofNat(12345);
const fixf = T.apply(F, f);
const before = T.allocs;
const st = T.makeStats();
// run only until f is applied to fix f: use onReduce to stop? Instead count total for the 13-step prefix via jet that stops.
let stopped = null;
try {
  T.apply(fixf, x, { jet: (a, b, stack, st2) => { if (a === f && b === fixf) { stopped = st2.steps; throw 'stop'; } return null; } }, st);
} catch (e) { if (e !== 'stop') throw e; }
console.log('concrete: steps until reduce(f, fix f):', stopped - 1, 'new nodes allocated:', T.allocs - before);
