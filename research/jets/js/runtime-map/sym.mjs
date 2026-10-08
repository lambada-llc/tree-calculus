// Symbolic execution of the eager TC machine (Machine.lean `step`, = the C++
// apply() loop without memo/jets) over hash-consed trees with hole atoms.
// Derives 1st-order "bigger step" rules: apply(P[holes], x) runs a fixed number
// of machine steps (independent of the holes) until it either halts with a
// value over holes, or needs the shape of a hole (split).
import fs from 'node:fs';

// ---- hash-consed node store: kind 0 leaf, 1 stem, 2 fork, 3 hole ----
export const K = [0, 0], U = [0, 0], Vv = [0, 0], CONC = [true, true], SIZE = [0, 1];
const table = new Map();
export const LEAF = 1;
export const holeName = new Map();
export function mk(k, u = 0, v = 0) {
  const key = k * 1e14 + u * 1e7 + v; // ids stay < 1e7 here
  let id = table.get(key);
  if (id !== undefined) return id;
  id = K.length;
  K.push(k); U.push(u); Vv.push(v);
  CONC.push(k === 1 ? CONC[u] : k === 2 ? CONC[u] && CONC[v] : false);
  SIZE.push(k === 1 ? SIZE[u] + 1 : k === 2 ? SIZE[u] + SIZE[v] + 1 : 0);
  table.set(key, id);
  return id;
}
let holeCounter = 0;
export function hole(name) {
  const id = mk(3, ++holeCounter, 0);
  holeName.set(id, name ?? `h${holeCounter}`);
  return id;
}

// ---- the machine ----
// frames: ['AT', arg] = APPLY_TO(arg); ['CA', fn, arg] = COMPUTE_AND_APPLY(fn, arg)
const memo = new Map();
export function run(a, b, { maxSteps = 1e8, useMemo = false, stack = [] } = {}) {
  let steps = 0, r = 0;
  const rules = [0, 0, 0, 0, 0, 0, 0];
  let mode = 'reduce';
  for (;;) {
    if (mode === 'reduce') {
      if (steps >= maxSteps) return { kind: 'budget', steps, rules };
      const memoable = useMemo && CONC[a] && CONC[b];
      if (memoable) {
        const hit = memo.get(a * 1e7 + b);
        if (hit !== undefined) { r = hit; mode = 'dispatch'; continue; }
      }
      const ka = K[a];
      if (ka === 3) return { kind: 'split', hole: a, state: { tag: 'reduce', a, b, stack }, steps, rules };
      if (ka === 0) { steps++; rules[0]++; r = mk(1, b); mode = 'dispatch'; continue; }
      if (ka === 1) { steps++; rules[1]++; r = mk(2, U[a], b); mode = 'dispatch'; continue; }
      const u = U[a], y = Vv[a], ku = K[u];
      if (ku === 3) return { kind: 'split', hole: u, state: { tag: 'reduce', a, b, stack }, steps, rules };
      if (ku === 0) { steps++; rules[2]++; r = y; mode = 'dispatch'; continue; }
      if (ku === 1) {
        if (memoable) stack.push(['M', a, b]);
        steps++; rules[3]++; stack.push(['CA', U[u], b]); a = y; continue;
      }
      const kb = K[b];
      if (kb === 3) return { kind: 'split', hole: b, state: { tag: 'reduce', a, b, stack }, steps, rules };
      if (kb === 0) { steps++; rules[4]++; r = U[u]; mode = 'dispatch'; continue; }
      if (memoable) stack.push(['M', a, b]);
      if (kb === 1) { steps++; rules[5]++; a = Vv[u]; b = U[b]; continue; }
      steps++; rules[6]++; stack.push(['AT', Vv[b]]); a = y; b = U[b]; continue;
    }
    // dispatch
    if (stack.length === 0) return { kind: 'halt', value: r, steps, rules };
    const f = stack.pop();
    if (f[0] === 'M') { memo.set(f[1] * 1e7 + f[2], r); continue; }
    mode = 'reduce';
    if (f[0] === 'AT') { a = r; b = f[1]; }
    else { stack.push(['AT', r]); a = f[1]; b = f[2]; }
  }
}
export function applyConc(a, b) {
  const res = run(a, b, { useMemo: true });
  if (res.kind !== 'halt') throw new Error('concrete apply did not halt: ' + res.kind);
  return res.value;
}

// ---- DAG parsing (3-word = apply, 2-word = alias, 1-word = value) ----
export const env = new Map([['△', LEAF]]);
export const names = new Map(); // node -> preferred name
export function loadDag(text, { prefer = () => true } = {}) {
  let value = 0;
  for (const line of text.split('\n')) {
    const w = line.trim().split(/\s+/).filter(Boolean);
    const get = (n) => { const t = env.get(n); if (t === undefined) throw new Error('unbound ' + n); return t; };
    if (w.length === 3) env.set(w[0], applyConc(get(w[1]), get(w[2])));
    else if (w.length === 2) {
      const t = get(w[1]); env.set(w[0], t);
      if (!/^\d+$/.test(w[0]) && prefer(w[0]) && (!names.has(t) || w[0].length < names.get(t).length)) names.set(t, w[0]);
    } else if (w.length === 1) value = get(w[0]);
  }
  return value;
}

// ---- printing ----
export function show(t, depth = 0) {
  if (K[t] === 3) return holeName.get(t);
  if (t === LEAF) return '△';
  if (names.has(t)) return names.get(t);
  if (CONC[t] && SIZE[t] > 9) return `#${t}[${SIZE[t]}]`;
  const p = (x) => { const s = show(x, depth + 1); return /\s/.test(s) ? `(${s})` : s; };
  if (K[t] === 1) return `△ ${p(U[t])}`;
  return `△ ${p(U[t])} ${p(Vv[t])}`;
}
// application-term AST: {v: node} | {f, x}
export function stateTerm(state) {
  let t = state.tag === 'reduce' ? { f: { v: state.a }, x: { v: state.b } } : { v: state.r };
  for (let i = state.stack.length - 1; i >= 0; i--) {
    const fr = state.stack[i];
    if (fr[0] === 'AT') t = { f: t, x: { v: fr[1] } };
    else t = { f: { f: { v: fr[1] }, x: { v: fr[2] } }, x: t };
  }
  return t;
}
export function showTerm(t) {
  if (t.v !== undefined) return show(t.v);
  const f = showTerm(t.f);
  let x = showTerm(t.x);
  if (t.x.v === undefined || /\s/.test(x)) x = `(${x})`;
  return `${f} ${x}`;
}

// ---- pattern statistics ----
// Nodes of value p that contain a hole (each one is an arena read + compares to match).
export function openNodes(p, acc = new Set()) {
  if (CONC[p] || K[p] === 3 || acc.has(p)) return acc;
  acc.add(p);
  openNodes(U[p], acc);
  if (K[p] === 2) openNodes(Vv[p], acc);
  return acc;
}
export function stateValues(state) {
  const vs = state.tag === 'reduce' ? [state.a, state.b] : [state.r];
  for (const fr of state.stack) vs.push(...fr.slice(1));
  return vs;
}

// substitute hole h := q in node t
export function subst(t, h, q, cache = new Map()) {
  if (CONC[t]) return t;
  if (t === h) return q;
  if (K[t] === 3) return t;
  if (cache.has(t)) return cache.get(t);
  const r = K[t] === 1 ? mk(1, subst(U[t], h, q, cache)) : mk(2, subst(U[t], h, q, cache), subst(Vv[t], h, q, cache));
  cache.set(t, r);
  return r;
}
export function substState(state, h, q) {
  const c = new Map(), s = (x) => subst(x, h, q, c);
  return state.tag === 'reduce'
    ? { tag: 'reduce', a: s(state.a), b: s(state.b), stack: state.stack.map((f) => [f[0], ...f.slice(1).map(s)]) }
    : { tag: 'dispatch', r: s(state.r), stack: state.stack.map((f) => [f[0], ...f.slice(1).map(s)]) };
}
export function resume(state, opts = {}) {
  if (state.tag === 'reduce') return run(state.a, state.b, { ...opts, stack: state.stack.map((f) => [...f]) });
  // dispatch: emulate by running a no-op? handle directly
  const stack = state.stack.map((f) => [...f]);
  if (stack.length === 0) return { kind: 'halt', value: state.r, steps: 0, rules: [0, 0, 0, 0, 0, 0, 0] };
  const f = stack.pop();
  if (f[0] === 'AT') return run(state.r, f[1], { ...opts, stack });
  stack.push(['AT', state.r]);
  return run(f[1], f[2], { ...opts, stack });
}
