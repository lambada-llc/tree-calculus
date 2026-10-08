import * as T from './tc.mjs';
import * as S from './sym.mjs';
const t0 = Date.now();
const env = T.loadBundle('/home/user/forest/src/.dag-bundle-arboretum-reduced');
console.log('loaded', env.size, 'names,', T.nodeCount(), 'nodes,', Date.now() - t0, 'ms');
const names = (i) => ['f', 'x', 'y', 'z'][i] ?? 'v' + i;
for (const n of ['fix', 'fix__wait', 'fix__naive']) {
  const F = env.get(n);
  console.log(`\n== ${n}: DAG size ${T.size(F)}`);
  // fix f: apply the combinator to a hole
  const d1 = S.derive(S.reduce(S.lit(F), S.svar(0)));
  console.log(`${n} f:`, S.showTree(d1, names).trim());
  if (d1.stop !== 'halt') continue;
  const P = d1.s.r;
  S.litNames.clear();
  // apply `fix f` to a hole x
  const d2 = S.derive(S.reduce(P, S.svar(1)));
  console.log(`(${n} f) x:`, S.showTree(d2, names).trim());
  // is the residual's second operand P itself?
  if (d2.s.t === 'reduce') console.log('residual b is the pattern itself:', JSON.stringify(d2.s.b) === JSON.stringify(P));
}
