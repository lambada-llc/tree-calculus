import * as T from './tc.mjs';
import * as S from './sym.mjs';
const env = T.loadBundle('/home/user/forest/src/.dag-bundle-arboretum-reduced');
const nm = (i) => (i === 0 ? 'x' : 'h' + i);
// Memo entries a fix-heavy run would record: heads `fix f` for several f.
const fix = env.get('fix');
const fs = ['Nat.add', 'List.map', 'equal', 'Nat.mul'].map((n) => env.get(n));
const heads = fs.map((f) => T.apply(fix, f));
let P = S.lit(heads[0]);
for (const h of heads.slice(1)) P = S.lgg(P, h, { i: 1 });
P = S.renumber(P);
console.log('lgg of fix f for 4 f:', S.showS(P, nm), ' holes', S.maxVar(P));
const viewed = new Set();
const tr = S.derive(S.reduce(P, S.svar(0)));
console.log('rule:', S.showTree(tr, nm).trim());
// Nat.add m for several m: lgg and the rule's decision tree when the argument is split
const add = env.get('Nat.add');
const addHeads = [3, 5, 6, 1000].map((m) => T.apply(add, T.ofNat(m)));
let Q = S.lit(addHeads[0]);
for (const h of addHeads.slice(1)) Q = S.lgg(Q, h, { i: 1 });
Q = S.renumber(Q);
console.log('\nlgg of Nat.add m for m in 3,5,6,1000:', S.showS(Q, nm));
const tr2 = S.derive(S.reduce(Q, S.svar(0)), { policy: (o) => o.where === 'arg', maxSplits: 2 });
console.log(S.showTree(tr2, nm));
