// Concrete tree calculus: hash-consed values and the eager machine of Machine.lean (`step`),
// with optional memo (RStep's hit/miss/put) and optional jets (RStep's jet), and counters.
//
// Value ids: 1 = leaf; node id -> (L, R): leaf (0,0), stem (u,0), fork (u,v).
import { readFileSync } from 'node:fs';

export const LEAF = 1;
export let L = new Int32Array(1 << 20), R = new Int32Array(1 << 20);
let next = 2;
const table = new Map();
const K = 67108864; // ids < 2^26 keep u*K+v exact
function grow() {
  const L2 = new Int32Array(L.length * 2); L2.set(L); L = L2;
  const R2 = new Int32Array(R.length * 2); R2.set(R); R = R2;
}
export let allocs = 0; // nodes newly created (hash-cons misses)
export let interns = 0; // hash-cons lookups
function intern(u, v) {
  interns++;
  const key = u * K + v;
  let id = table.get(key);
  if (id === undefined) {
    if (next >= L.length) grow();
    id = next++; L[id] = u; R[id] = v; table.set(key, id); allocs++;
    if (next >= K) throw new Error('arena too big for key packing');
  }
  return id;
}
export const stem = (u) => intern(u, 0);
export const fork = (u, v) => intern(u, v);
export const isLeaf = (x) => L[x] === 0;
export const isStem = (x) => L[x] !== 0 && R[x] === 0;
export const isFork = (x) => R[x] !== 0;
export const nodeCount = () => next - 2;

export function ofTernary(s) {
  let i = 0;
  const go = () => {
    const stack = [];
    // iterative prefix parse
    const out = [];
    const ops = [];
    for (;;) {
      const c = s[i++];
      if (c === '0') { out.push(LEAF); }
      else if (c === '1') { ops.push([1, out.length]); continue; }
      else if (c === '2') { ops.push([2, out.length]); continue; }
      else throw new Error('bad ternary at ' + (i - 1));
      // reduce completed ops
      while (ops.length) {
        const [k, base] = ops[ops.length - 1];
        if (out.length - base < k) break;
        ops.pop();
        if (k === 1) { const x = out.pop(); out.push(stem(x)); }
        else { const y = out.pop(); const x = out.pop(); out.push(fork(x, y)); }
      }
      if (!ops.length) return out[0];
    }
  };
  return go();
}
export function toTernary(x) {
  let s = '';
  const st = [x];
  while (st.length) {
    const t = st.pop();
    if (isLeaf(t)) s += '0';
    else if (isStem(t)) { s += '1'; st.push(L[t]); }
    else { s += '2'; st.push(R[t]); st.push(L[t]); }
  }
  return s;
}
export function ofNat(n) {
  let r = LEAF; const bits = [];
  while (n > 0) { bits.push(n & 1); n = Math.floor(n / 2); }
  for (let i = bits.length - 1; i >= 0; i--) r = fork(bits[i] ? stem(LEAF) : LEAF, r);
  return r;
}
export function toNat(x) {
  let n = 0, p = 1;
  while (isFork(x)) { if (isStem(L[x])) n += p; else if (!isLeaf(L[x])) return NaN; p *= 2; x = R[x]; }
  return isLeaf(x) ? n : NaN;
}
export function size(x, seen = new Set()) { // distinct nodes (DAG size), leaf excluded
  const st = [x]; let n = 0;
  while (st.length) { const t = st.pop(); if (t === LEAF || seen.has(t)) continue; seen.add(t); n++; st.push(L[t]); if (R[t]) st.push(R[t]); }
  return n;
}
export function treeSize(x, memo = new Map()) { // nodes of the expanded tree, incl. leaves
  if (isLeaf(x)) return 1;
  if (memo.has(x)) return memo.get(x);
  const r = 1 + treeSize(L[x], memo) + (R[x] ? treeSize(R[x], memo) : 0);
  memo.set(x, r); return r;
}

// ---- the machine (Machine.lean `step`), frames: [APPLY_TO, arg] / [CAA, fn, arg] / [MEMO, a, b, stepsAtPush]
export const APPLY_TO = 0, CAA = 1, MEMO = 2;
export function makeStats() { return { steps: 0, transitions: 0, memoHits: 0, memoPuts: 0, jets: 0, jetSaved: 0 }; }

/**
 * apply(a, b) by `step`. opts.memo: Map or null. opts.jet(a, b, stack, st) -> null | {kind:'reduce', a, b} | {kind:'dispatch', r}
 * (the jet may push frames on `stack`). opts.onReduce(a, b, stack) observer.
 */
export function apply(a, b, opts = {}, st = makeStats()) {
  const stack = [];
  const memo = opts.memo || null;
  const jet = opts.jet || null;
  const onReduce = opts.onReduce || null;
  const memoMin = opts.memoMin ?? 1;
  let r;
  reduce: for (;;) {
    st.steps++; st.transitions++;
    if (onReduce) onReduce(a, b, stack);
    if (jet) {
      const j = jet(a, b, stack, st);
      if (j) {
        st.jets++;
        if (j.kind === 'reduce') { a = j.a; b = j.b; continue reduce; }
        r = j.r;
      }
    }
    if (r === undefined) {
      const au = L[a], ay = R[a];
      if (au === 0) r = stem(b);
      else if (ay === 0) r = fork(au, b);
      else {
        const uu = L[au], uv = R[au];
        if (uu === 0) r = ay;
        else if (uv === 0) {
          if (memo) { const h = memo.get(a * K + b); if (h !== undefined) { st.memoHits++; r = h; } else stack.push([MEMO, a, b, st.steps]); }
          if (r === undefined) { stack.push([CAA, uu, b]); a = ay; continue reduce; }
        } else {
          if (b === LEAF) r = uu;
          else if (R[b] === 0) {
            if (memo) { const h = memo.get(a * K + b); if (h !== undefined) { st.memoHits++; r = h; } else stack.push([MEMO, a, b, st.steps]); }
            if (r === undefined) { a = uv; b = L[b]; continue reduce; }
          } else {
            if (memo) { const h = memo.get(a * K + b); if (h !== undefined) { st.memoHits++; r = h; } else stack.push([MEMO, a, b, st.steps]); }
            if (r === undefined) { stack.push([APPLY_TO, R[b]]); a = ay; b = L[b]; continue reduce; }
          }
        }
      }
    }
    // dispatch
    for (;;) {
      if (!stack.length) return r;
      const f = stack.pop();
      st.transitions++;
      if (f[0] === MEMO) { if (st.steps - f[3] >= memoMin) { memo.set(f[1] * K + f[2], r); st.memoPuts++; } continue; }
      if (f[0] === APPLY_TO) { a = r; b = f[1]; r = undefined; continue reduce; }
      stack.push([APPLY_TO, r]); a = f[1]; b = f[2]; r = undefined; continue reduce;
    }
  }
}

// ---- DAG bundles: `id l r` = apply(l, r); `sym id` alias; `△` leaf.
export function loadBundle(path, opts = {}) {
  const env = new Map([['△', LEAF]]);
  const text = readFileSync(path, 'utf8');
  for (const line of text.split('\n')) {
    const w = line.trim().split(/\s+/).filter(Boolean);
    if (w.length === 3) {
      const f = env.get(w[1]), x = env.get(w[2]);
      if (f === undefined || x === undefined) throw new Error('unbound in ' + line);
      env.set(w[0], apply(f, x, opts));
    } else if (w.length === 2) {
      const v = env.get(w[1]);
      if (v === undefined) throw new Error('unbound alias ' + line);
      env.set(w[0], v);
    }
  }
  return env;
}

export function show(x, depth = 6) {
  if (isLeaf(x)) return '△';
  if (depth === 0) return '…';
  if (isStem(x)) return `(△ ${show(L[x], depth - 1)})`;
  return `(△ ${show(L[x], depth - 1)} ${show(R[x], depth - 1)})`;
}
