// Derive the order-1 rules of arboretum's core combinators applied to holes, one argument at a time.
import * as T from './tc.mjs';
import * as S from './sym.mjs';
const env = T.loadBundle('/home/user/forest/src/.dag-bundle-arboretum-reduced');
const nm = (i) => 'abcdefgh'[i] ?? 'v' + i;
for (const [name, arity] of [['compose', 3], [':s', 3], [':c', 3], ['const', 2], ['let', 2], ['wait', 3], ['id', 1], ['self_apply', 1], ['triage', 4], ['Bool.not', 1], ['fix', 2]]) {
  let F = env.get(name);
  if (F === undefined) { console.log(name, 'missing'); continue; }
  let head = S.lit(F); const parts = [];
  for (let i = 0; i < arity; i++) {
    const tr = S.derive(S.reduce(head, S.svar(i)));
    parts.push(`${tr.steps}${tr.stop === 'halt' ? '' : '→' + tr.stop}`);
    if (tr.stop !== 'halt') { parts.push('residual: ' + S.showState(tr.s, nm)); break; }
    head = tr.s.r;
  }
  console.log(`${name.padEnd(10)} reduce steps per argument: ${parts.join(' | ')}`);
}
