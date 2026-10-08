// Pattern rules (holes in the head) vs ground-head rules: same firings, far fewer derivations.
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
};
const FUEL = Number(process.env.FUEL || 1024);
function match(P, x, sig) {
  switch (P.t) {
    case 'lit': return x === P.v;
    case 'var': sig[P.i] = x; return true;
    case 'stem': return T.isStem(x) && match(P.u, T.L[x], sig);
    case 'fork': return T.isFork(x) && match(P.u, T.L[x], sig) && match(P.v, T.R[x], sig);
  }
}
function matchCost(P) { // nodes walked: hole-free subtrees are one id comparison
  if (P.t === 'lit' || P.t === 'var') return 1;
  if (P.t === 'stem') return 1 + matchCost(P.u);
  return 1 + matchCost(P.u) + matchCost(P.v);
}
const canon = (P) => P.t === 'stem' ? S.cstem(canon(P.u)) : P.t === 'fork' ? S.cfork(canon(P.u), canon(P.v)) : P;
const pushFrames = (stack, k, sig) => {
  for (let j = k.length - 1; j >= 0; j--) {
    const f = k[j];
    if (f.t === 'applyTo') stack.push([T.APPLY_TO, S.subst(f.arg, sig)]);
    else stack.push([T.CAA, S.subst(f.fn, sig), S.subst(f.arg, sig)]);
  }
};
for (const [name, [prog, mkArg]] of Object.entries(cases)) {
  const f = T.apply(I, T.ofTernary(prog));
  const x = mkArg();
  const base = T.makeStats();
  const r0 = T.apply(f, x, {}, base);
  // pattern-rule machine
  const pats = []; // {P, rule, steps}
  const byHead = new Map(); // head -> {pat, sig} | null
  const c = { derivTransitions: 0, derivations: 0, matchWork: 0, headsSeen: 0, groundDerivTransitions: 0 };
  const jet = (a, b, stack, st) => {
    let hit = byHead.get(a);
    if (hit === undefined) {
      c.headsSeen++;
      hit = null;
      for (const p of pats) { const sig = []; c.matchWork += 1; if (match(p.P, a, sig)) { c.matchWork += p.cost; hit = { p, sig }; break; } }
      if (!hit) {
        const viewed = new Set();
        S.hooks.onView = (v) => viewed.add(v);
        const tr = S.derive(S.reduce(S.lit(a), S.svar(0)), { fuel: FUEL });
        S.hooks.onView = null;
        c.derivations++; c.derivTransitions += tr.transitions + 1;
        if (tr.steps >= 2) {
          const P = canon(S.generalize(a, viewed, { i: 1 }));
          const rule = S.derive(S.reduce(P, S.svar(0)), { fuel: FUEL });
          c.derivTransitions += rule.transitions + 1;
          if (rule.steps !== tr.steps) throw new Error('generalized rule differs');
          const p = { P, rule, cost: matchCost(P) };
          pats.push(p);
          const sig = []; match(P, a, sig); hit = { p, sig };
        }
      }
      byHead.set(a, hit);
    }
    if (!hit) return null;
    const { p, sig } = hit;
    const sg = (i) => (i === 0 ? b : sig[i]);
    st.jetSaved += p.rule.steps - 1;
    const s = p.rule.s;
    pushFrames(stack, s.k, sg);
    if (s.t === 'reduce') return { kind: 'reduce', a: S.subst(s.a, sg), b: S.subst(s.b, sg) };
    return { kind: 'dispatch', r: S.subst(s.r, sg) };
  };
  const st = T.makeStats();
  const r = T.apply(f, x, { jet }, st);
  if (r !== r0) throw new Error(name + ': pattern jets changed the result');
  // ground-head machine for comparison of derivation cost
  const ground = new Map(); let gd = 0, gdt = 0;
  const st2 = T.makeStats();
  T.apply(f, x, { jet: (a, b, stack, s2) => {
    let tr = ground.get(a);
    if (tr === undefined) { tr = S.derive(S.reduce(S.lit(a), S.svar(0)), { fuel: FUEL }); ground.set(a, tr); gd++; gdt += tr.transitions + 1; }
    if (tr.steps < 2) return null;
    const sg = (i) => b; s2.jetSaved += tr.steps - 1;
    pushFrames(stack, tr.s.k, sg);
    return tr.s.t === 'reduce' ? { kind: 'reduce', a: S.subst(tr.s.a, sg), b: S.subst(tr.s.b, sg) } : { kind: 'dispatch', r: S.subst(tr.s.r, sg) };
  } }, st2);
  console.log(`${name.padEnd(15)} plain ${base.steps} | ground-head rules: steps ${st2.steps} (×${(base.steps / st2.steps).toFixed(1)}), derivations ${gd}, derivation transitions ${gdt} | ` +
    `pattern rules: steps ${st.steps} (×${(base.steps / st.steps).toFixed(1)}), patterns ${pats.length}, derivations ${c.derivations}, derivation transitions ${c.derivTransitions}, heads ${c.headsSeen}, match work ${c.matchWork}`);
}
