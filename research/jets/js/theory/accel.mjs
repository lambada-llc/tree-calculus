// A rule-accelerated machine: RStep's `jet` transitions taken from derived rules.
//
// Rule families measured:
//   'head'  — ground head, parametric argument: for each head value `a` met at a `reduce`, the
//             deterministic prefix of reduce(lit a, var 0) up to the first split on var 0
//             (or halt / fuel). Order 1 in the argument only; one rule per distinct head.
//   'shape' — the same, but splitting on var 0 (the argument) up to `depth` times: a decision
//             tree keyed on the argument's top shape (order 2).
// Each rule is used only if it saves >= minSave reduce steps.
import * as T from './tc.mjs';
import * as S from './sym.mjs';

const pushFrames = (stack, k, sigma) => {
  for (let j = k.length - 1; j >= 0; j--) {
    const f = k[j];
    if (f.t === 'applyTo') stack.push([T.APPLY_TO, S.subst(f.arg, sigma)]);
    else stack.push([T.CAA, S.subst(f.fn, sigma), S.subst(f.arg, sigma)]);
  }
};

/** Walk a decision tree on concrete sigma: return the reached rule leaf and the extended sigma. */
function select(tr, sigma) {
  let env = new Map(); // var -> value, for vars introduced by splits
  const look = (i) => (i === 0 ? sigma : env.get(i));
  while (tr.t === 'case') {
    const v = look(tr.i);
    const j = T.isLeaf(v) ? 0 : T.isStem(v) ? 1 : 2;
    const p = tr.pats[j];
    if (j === 1) env.set(p.u.i, T.L[v]);
    if (j === 2) { env.set(p.u.i, T.L[v]); env.set(p.v.i, T.R[v]); }
    tr = tr.branches[j];
  }
  return { leaf: tr, sig: (i) => look(i) };
}

export function makeJet({ fuel = 1000, depth = 0, minSave = 2, stats }) {
  const rules = new Map(); // head id -> decision tree
  const policy = (o) => o.where === 'arg';
  return (a, b, stack, st) => {
    let tr = rules.get(a);
    if (tr === undefined) {
      tr = S.derive(S.reduce(S.lit(a), S.svar(0)), { fuel, policy, maxSplits: depth });
      rules.set(a, tr);
      stats.rules++;
    }
    const { leaf, sig } = select(tr, b);
    if (leaf.steps < minSave) return null;
    st.jetSaved += leaf.steps - 1;
    stats.residualNodes += countSym(leaf.s);
    stats.frames += leaf.s.k.length;
    stats.savedHist[Math.min(leaf.steps, 1 << 20)] = (stats.savedHist[Math.min(leaf.steps, 1 << 20)] || 0) + 1;
    stats.depthSum += leaf.depth ?? 0;
    const s = leaf.s;
    pushFrames(stack, s.k, sig);
    if (s.t === 'reduce') return { kind: 'reduce', a: S.subst(s.a, sig), b: S.subst(s.b, sig) };
    return { kind: 'dispatch', r: S.subst(s.r, sig) };
  };
}

// nodes of a residual that mention a variable (what instantiating it has to build)
function countSym(s) {
  let n = 0;
  const c = (x) => { if (x.t === 'stem') { n++; c(x.u); } else if (x.t === 'fork') { n++; c(x.u); c(x.v); } };
  S.mapState(s, (x) => { c(x); return x; });
  return n;
}
