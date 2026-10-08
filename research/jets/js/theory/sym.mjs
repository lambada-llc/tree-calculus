// Symbolic execution, a transcription of Symbolic.lean (`SValue`, `view`, `sstep`) and Check.lean
// (`explore`, `shapes`), plus rule derivation (the deterministic prefix / decision tree).
import * as T from './tc.mjs';
const { L, R } = T;

// SValue: {t:'lit', v} | {t:'stem', u} | {t:'fork', u, v} | {t:'var', i}
export const lit = (v) => ({ t: 'lit', v });
export const sstem = (u) => ({ t: 'stem', u });
export const sfork = (u, v) => ({ t: 'fork', u, v });
export const svar = (i) => ({ t: 'var', i });
export const LEAFS = lit(T.LEAF);

// smart constructors that collapse var-free structure into lits (canonical form, same instances)
export const opts = { collapse: true }; // false: build exactly as Symbolic.lean's sstep does
export function cstem(u) { return opts.collapse && u.t === 'lit' ? lit(T.stem(u.v)) : sstem(u); }
export function cfork(u, v) { return opts.collapse && u.t === 'lit' && v.t === 'lit' ? lit(T.fork(u.v, v.v)) : sfork(u, v); }

// view: one level of shape (Symbolic.lean `view`). The lit case reads the arena: T.L/T.R may be
// reallocated by growth, so always go through the module bindings.
export const hooks = { onView: null };
export function view(a) {
  switch (a.t) {
    case 'lit': {
      const x = a.v;
      if (hooks.onView) hooks.onView(x);
      if (T.L[x] === 0) return { t: 'leaf' };
      if (T.R[x] === 0) return { t: 'stem', u: lit(T.L[x]) };
      return { t: 'fork', u: lit(T.L[x]), v: lit(T.R[x]) };
    }
    case 'stem': return { t: 'stem', u: a.u };
    case 'fork': return { t: 'fork', u: a.u, v: a.v };
    case 'var': return { t: 'var', i: a.i };
  }
}

// States: {t:'reduce', a, b, k} | {t:'dispatch', r, k}; frames {t:'applyTo', arg} | {t:'cAA', fn, arg}
// k is an immutable array, top first (as in Lean's List).
export const reduce = (a, b, k = []) => ({ t: 'reduce', a, b, k });
export const dispatch = (r, k = []) => ({ t: 'dispatch', r, k });

/** sstep, exactly Symbolic.lean's, with the split classified: where = 'head' (a = var),
 *  'kind' (a = fork(var, _)), 'arg' (triage on b = var). Builds with cstem/cfork, which is
 *  `step`'s .stem b / .fork u b up to the lit/structure identification (same subst). */
export function sstep(s) {
  if (s.t === 'reduce') {
    const { a, b, k } = s;
    const av = view(a);
    switch (av.t) {
      case 'var': return { t: 'split', i: av.i, where: 'head' };
      case 'leaf': return { t: 'next', s: dispatch(cstem(b), k), rule: '0a' };
      case 'stem': return { t: 'next', s: dispatch(cfork(av.u, b), k), rule: '0b' };
      case 'fork': {
        const uv = view(av.u), y = av.v;
        switch (uv.t) {
          case 'var': return { t: 'split', i: uv.i, where: 'kind' };
          case 'leaf': return { t: 'next', s: dispatch(y, k), rule: '1' };
          case 'stem': return { t: 'next', s: reduce(y, b, [{ t: 'cAA', fn: uv.u, arg: b }, ...k]), rule: '2' };
          case 'fork': {
            const w = uv.u, x = uv.v, bv = view(b);
            switch (bv.t) {
              case 'var': return { t: 'split', i: bv.i, where: 'arg' };
              case 'leaf': return { t: 'next', s: dispatch(w, k), rule: '3a' };
              case 'stem': return { t: 'next', s: reduce(x, bv.u, k), rule: '3b' };
              case 'fork': return { t: 'next', s: reduce(y, bv.u, [{ t: 'applyTo', arg: bv.v }, ...k]), rule: '3c' };
            }
          }
        }
      }
    }
  }
  if (s.k.length === 0) return { t: 'halt' };
  const [f, ...k] = s.k;
  if (f.t === 'applyTo') return { t: 'next', s: reduce(s.r, f.arg, k), rule: 'pop' };
  return { t: 'next', s: reduce(f.fn, f.arg, [{ t: 'applyTo', arg: s.r }, ...k]), rule: 'pop' };
}

// ---- substitution / instantiation
export function subst(x, sigma) {
  switch (x.t) {
    case 'lit': return x.v;
    case 'stem': return T.stem(subst(x.u, sigma));
    case 'fork': return T.fork(subst(x.u, sigma), subst(x.v, sigma));
    case 'var': { const v = sigma(x.i); if (v === undefined) throw new Error('unbound var ' + x.i); return v; }
  }
}
export function inst(x, i, p) {
  switch (x.t) {
    case 'lit': return x;
    case 'stem': return cstem(inst(x.u, i, p));
    case 'fork': return cfork(inst(x.u, i, p), inst(x.v, i, p));
    case 'var': return x.i === i ? p : x;
  }
}
const mapFrame = (f, g) => f.t === 'applyTo' ? { t: 'applyTo', arg: g(f.arg) } : { t: 'cAA', fn: g(f.fn), arg: g(f.arg) };
export function mapState(s, g) {
  return s.t === 'reduce' ? reduce(g(s.a), g(s.b), s.k.map((f) => mapFrame(f, g)))
                          : dispatch(g(s.r), s.k.map((f) => mapFrame(f, g)));
}
export function maxVar(x) {
  switch (x.t) {
    case 'lit': return -1;
    case 'stem': return maxVar(x.u);
    case 'fork': return Math.max(maxVar(x.u), maxVar(x.v));
    case 'var': return x.i;
  }
}
export function stateMaxVar(s) {
  let m = -1; mapState(s, (x) => { m = Math.max(m, maxVar(x)); return x; }); return m;
}
export const shapes = (j) => [LEAFS, sstem(svar(j)), sfork(svar(j), svar(j + 1))];

// ---- printing
export function showS(x, names = (i) => 'x' + i, d = 8) {
  switch (x.t) {
    case 'lit': return litName(x.v, d);
    case 'stem': return `(△ ${showS(x.u, names, d)})`;
    case 'fork': return `(△ ${showS(x.u, names, d)} ${showS(x.v, names, d)})`;
    case 'var': return names(x.i);
  }
}
export const litNames = new Map();
function litName(v, d) {
  if (litNames.has(v)) return litNames.get(v);
  if (T.L[v] === 0) return '△';
  const sz = T.size(v);
  if (sz > 6) return `#${v}[${sz}]`;
  return T.show(v, d);
}
export function showState(s, names) {
  const fr = s.k.map((f) => f.t === 'applyTo' ? `applyTo ${showS(f.arg, names)}` : `cAA ${showS(f.fn, names)} ${showS(f.arg, names)}`);
  const head = s.t === 'reduce' ? `reduce ${showS(s.a, names)} ${showS(s.b, names)}` : `dispatch ${showS(s.r, names)}`;
  return `${head} [${fr.join(', ')}]`;
}

// ---- derivation
/**
 * Run `s` symbolically: the deterministic prefix, with case splits where `policy(split, state)`
 * says so (Check.lean `explore`'s branching), stopping a branch at a split the policy declines,
 * at halt, or after `fuel` transitions. Returns a decision tree:
 *   {t:'rule', s, steps, transitions, stop}   stop: 'head'|'kind'|'arg' (declined split), 'halt', 'fuel'
 *   {t:'case', i, branches: [leaf, stem, fork]}
 */
export function derive(s, { fuel = 10000, policy = () => false, maxSplits = 8, stopAt = null } = {}, acc = { steps: 0, transitions: 0, splits: 0 }) {
  let steps = acc.steps, transitions = acc.transitions;
  for (let n = 0; n < fuel; n++) {
    if (stopAt && stopAt(s, steps)) return { t: 'rule', s, steps, transitions, stop: 'stopAt' };
    const o = sstep(s);
    if (o.t === 'next') { if (s.t === 'reduce') steps++; transitions++; s = o.s; continue; }
    if (o.t === 'halt') return { t: 'rule', s, steps, transitions, stop: 'halt' };
    // split
    if (acc.splits < maxSplits && policy(o, s)) {
      const j = stateMaxVar(s) + 1;
      return {
        t: 'case', i: o.i, where: o.where,
        branches: shapes(j).map((p) => derive(mapState(s, (x) => inst(x, o.i, p)), { fuel: fuel - n, policy, maxSplits, stopAt },
          { steps, transitions, splits: acc.splits + 1 })),
        pats: shapes(j),
      };
    }
    return { t: 'rule', s, steps, transitions, stop: o.where };
  }
  return { t: 'rule', s, steps, transitions, stop: 'fuel' };
}

export function showTree(tr, names, ind = '') {
  if (tr.t === 'rule') return `${ind}⟶ ${showState(tr.s, names)}   {${tr.steps} reduce steps, ${tr.transitions} transitions, stop: ${tr.stop}}\n`;
  let out = '';
  tr.branches.forEach((b, j) => {
    out += `${ind}${names(tr.i)} = ${showS(tr.pats[j], names)}:\n` + showTree(b, names, ind + '  ');
  });
  return out;
}

/** The pattern of `x` its run looked at: every subtree whose node was never viewed becomes a
 *  fresh variable (numbered from `next.i`). Sound by construction: the symbolic run over the
 *  pattern takes the same transitions, never splitting on the new variables. */
export function generalize(x, viewed, next) {
  if (!viewed.has(x)) return svar(next.i++);
  if (T.L[x] === 0) return LEAFS;
  if (T.R[x] === 0) return sstem(generalize(T.L[x], viewed, next));
  return sfork(generalize(T.L[x], viewed, next), generalize(T.R[x], viewed, next));
}
export function patKey(p) {
  switch (p.t) {
    case 'lit': return 'L' + p.v;
    case 'stem': return '(1 ' + patKey(p.u) + ')';
    case 'fork': return '(2 ' + patKey(p.u) + ' ' + patKey(p.v) + ')';
    case 'var': return '_';
  }
}
export function patSize(p) { // non-variable nodes the matcher has to check
  switch (p.t) {
    case 'lit': return 1;
    case 'stem': return 1 + patSize(p.u);
    case 'fork': return 1 + patSize(p.u) + patSize(p.v);
    case 'var': return 0;
  }
}

/** Plotkin/Reynolds anti-unification of a pattern and a value: the least general pattern of
 *  which both are instances. One fresh variable per differing position (linear: never needed
 *  to be shared for soundness, and a shared one would make matching test equality). */
export function lgg(p, x, next) {
  if (p.t === 'var') return svar(next.i++);
  if (p.t === 'lit' && p.v === x) return p;
  const pv = view(p);
  if (pv.t === 'leaf' && T.L[x] === 0) return LEAFS;
  if (pv.t === 'stem' && T.L[x] !== 0 && T.R[x] === 0) return cstem(lgg(pv.u, T.L[x], next));
  if (pv.t === 'fork' && T.R[x] !== 0) return cfork(lgg(pv.u, T.L[x], next), lgg(pv.v, T.R[x], next));
  return svar(next.i++);
}
export function renumber(p, next = { i: 1 }) {
  switch (p.t) {
    case 'var': return svar(next.i++);
    case 'stem': return sstem(renumber(p.u, next));
    case 'fork': return sfork(renumber(p.u, next), renumber(p.v, next));
    default: return p;
  }
}
