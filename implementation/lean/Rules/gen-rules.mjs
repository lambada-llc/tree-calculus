#!/usr/bin/env node
// Derive rules ("first-order jets") by running Symbolic.lean's `sstep` — transcribed exactly: no
// folding of variable-free structure into `lit`, so the right sides are the very states Lean's
// `ssteps` computes — and emit them as Lean, each with a `decide +kernel` check.
//
//   node gen-rules.mjs [options] <bundle.dag> <spec>... > Out.lean
//
//   spec       <symbol>/<arity>[/split]   apply <symbol> to var 0, var 1, …, one rule per argument
//                                          (a stage), until a stage stops at a split or <arity>;
//                                          `split` makes a stop at a triage of an argument a
//                                          `RuleTree` of three rules (one split).
//   --all <n>  every symbol of the bundle, arity <n> (no specs needed)
//   --ns <N>   Lean namespace (default Rules)
//   --min <ℓ>  drop rules shorter than ℓ transitions (default 1)
//   --max <n>  at most <n> rules (in bundle order)
//   --fuel <n> give up on a stage after <n> transitions (default 100000)
//   --rfl      prove each rule's run `ssteps ℓ lhs = some rhs` by `kernel_rfl` (Rules/KernelRfl.lean)
//              instead of `checkRule` by `decide +kernel`: no walk of the `lit`s in the comparison
//   --stats    print one TSV line per rule to stderr: name, ℓ, reduce steps, lit nodes, rhs size
//
// For a spec of arity n ≥ 2 there is also `<symbol>_app`: the whole application, `reduce <symbol>
// x0` with `x1 … x(n-1)` as `applyTo` frames, in one rule.
//
// Each 3-word line of the bundle is evaluated by the eager machine (`step`, no memo, 10^8 steps at
// most), so a canonical (unreduced) bundle works too if its definitions are cheap: extract the
// symbols wanted with `bin/dag.js extract` first (arboretum's whole canonical bundle is not). Every
// rule is cross-checked here on random instances (concrete `step`, exactly ℓ transitions) before
// it is written.

import { readFileSync } from 'node:fs';

const args = process.argv.slice(2);
const opt = { ns: 'Rules', min: 1, max: Infinity, all: 0, stats: false, fuel: 1e5, rfl: false };
const pos = [];
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  if (a === '--ns') opt.ns = args[++i];
  else if (a === '--min') opt.min = +args[++i];
  else if (a === '--max') opt.max = +args[++i];
  else if (a === '--all') opt.all = +args[++i];
  else if (a === '--stats') opt.stats = true;
  else if (a === '--rfl') opt.rfl = true;
  else if (a === '--fuel') opt.fuel = +args[++i];
  else pos.push(a);
}
const [file, ...specs] = pos;
if (!file || (!specs.length && !opt.all)) {
  console.error('usage: gen-rules.mjs [--ns N] [--all n] [--min l] [--max n] [--stats] <bundle.dag> <symbol>/<arity>[/split]...');
  process.exit(2);
}

// ---- values: hash-consed; 0 is △, a stem has R = -1
const L = [-1], R = [-1], interned = new Map();
const node = (l, r) => {
  const key = `${l},${r}`;
  let id = interned.get(key);
  if (id === undefined) { id = L.length; L.push(l); R.push(r); interned.set(key, id); }
  return id;
};
const LEAF = 0, stem = (u) => node(u, -1), fork = (u, v) => node(u, v);
const isStem = (x) => x > 0 && R[x] === -1;

// ---- Machine.lean's `step`, run to the end: apply(a, b)
function apply(a, b, budget = 1e8) {
  const k = []; // top last: [0, arg] applyTo, [1, fn, arg] computeAndApply
  let r, reducing = true;
  for (let n = 0; ; ) {
    if (reducing) {
      if (++n > budget) throw new Error('budget');
      if (a === LEAF) { r = stem(b); reducing = false; }
      else if (isStem(a)) { r = fork(L[a], b); reducing = false; }
      else {
        const u = L[a], y = R[a];
        if (u === LEAF) { r = y; reducing = false; }
        else if (isStem(u)) { k.push([1, L[u], b]); a = y; }
        else if (b === LEAF) { r = L[u]; reducing = false; }
        else if (isStem(b)) { a = R[u]; b = L[b]; }
        else { k.push([0, R[b]]); a = y; b = L[b]; }
      }
    } else {
      if (!k.length) return r;
      const f = k.pop();
      if (f[0] === 0) { a = r; b = f[1]; }
      else { k.push([0, r]); a = f[1]; b = f[2]; }
      reducing = true;
    }
  }
}

// ---- the bundle
const env = new Map([['△', LEAF]]);
const order = [];
for (const line of readFileSync(file, 'utf8').split('\n')) {
  const w = line.trim().split(/\s+/).filter(Boolean);
  const get = (id) => { const v = env.get(id); if (v === undefined) throw new Error(`unbound ${id}`); return v; };
  if (w.length === 3) env.set(w[0], apply(get(w[1]), get(w[2])));
  else if (w.length === 2) { env.set(w[0], get(w[1])); order.push(w[0]); }
}

// ---- Symbolic.lean: SValue, view, sstep, inst — exactly
const lit = (v) => ({ t: 'lit', v }), sstem = (u) => ({ t: 'stem', u });
const sfork = (u, v) => ({ t: 'fork', u, v }), svar = (i) => ({ t: 'var', i });
function view(a) {
  if (a.t !== 'lit') return a;
  const x = a.v;
  if (x === LEAF) return { t: 'leaf' };
  if (isStem(x)) return { t: 'stem', u: lit(L[x]) };
  return { t: 'fork', u: lit(L[x]), v: lit(R[x]) };
}
const reduce = (a, b, k) => ({ t: 'reduce', a, b, k });
const dispatch = (r, k) => ({ t: 'dispatch', r, k });
function sstep(s) {
  if (s.t === 'reduce') {
    const { a, b, k } = s, av = view(a);
    if (av.t === 'var') return { t: 'split', i: av.i, where: 'head' };
    if (av.t === 'leaf') return { t: 'next', s: dispatch(sstem(b), k), red: 1 };
    if (av.t === 'stem') return { t: 'next', s: dispatch(sfork(av.u, b), k), red: 1 };
    const uv = view(av.u), y = av.v;
    if (uv.t === 'var') return { t: 'split', i: uv.i, where: 'kind' };
    if (uv.t === 'leaf') return { t: 'next', s: dispatch(y, k), red: 1 };
    if (uv.t === 'stem') return { t: 'next', s: reduce(y, b, [{ t: 'caa', fn: uv.u, arg: b }, ...k]), red: 1 };
    const bv = view(b);
    if (bv.t === 'var') return { t: 'split', i: bv.i, where: 'arg' };
    if (bv.t === 'leaf') return { t: 'next', s: dispatch(uv.u, k), red: 1 };
    if (bv.t === 'stem') return { t: 'next', s: reduce(uv.v, bv.u, k), red: 1 };
    return { t: 'next', s: reduce(y, bv.u, [{ t: 'applyTo', arg: bv.v }, ...k]), red: 1 };
  }
  if (!s.k.length) return { t: 'halt' };
  const [f, ...k] = s.k;
  if (f.t === 'applyTo') return { t: 'next', s: reduce(s.r, f.arg, k), red: 0 };
  return { t: 'next', s: reduce(f.fn, f.arg, [{ t: 'applyTo', arg: s.r }, ...k]), red: 0 };
}
function inst(x, i, p) {
  switch (x.t) {
    case 'lit': return x;
    case 'stem': return sstem(inst(x.u, i, p));
    case 'fork': return sfork(inst(x.u, i, p), inst(x.v, i, p));
    case 'var': return x.i === i ? p : x;
  }
}
const mapFrame = (f, g) => f.t === 'applyTo' ? { t: 'applyTo', arg: g(f.arg) } : { t: 'caa', fn: g(f.fn), arg: g(f.arg) };
const mapState = (s, g) => s.t === 'reduce'
  ? reduce(g(s.a), g(s.b), s.k.map((f) => mapFrame(f, g))) : dispatch(g(s.r), s.k.map((f) => mapFrame(f, g)));
const maxVar = (x) => x.t === 'var' ? x.i : x.t === 'stem' ? maxVar(x.u) : x.t === 'fork' ? Math.max(maxVar(x.u), maxVar(x.v)) : -1;
const stateMaxVar = (s) => { let m = -1; mapState(s, (x) => { m = Math.max(m, maxVar(x)); return x; }); return m; };

/** `ssteps` until a split or halt: the residual, its length ℓ, reduce steps, why it stopped. */
function run(s, fuel = opt.fuel) {
  let n = 0, red = 0;
  for (; n < fuel; n++) {
    const o = sstep(s);
    if (o.t !== 'next') return { s, n, red, stop: o };
    s = o.s; red += o.red;
  }
  throw new Error('fuel');
}

// ---- cross-check on instances: concrete `step`, exactly ℓ transitions
function subst(x, σ) {
  switch (x.t) {
    case 'lit': return x.v;
    case 'stem': return stem(subst(x.u, σ));
    case 'fork': return fork(subst(x.u, σ), subst(x.v, σ));
    case 'var': return σ(x.i);
  }
}
function stepC(s) { // `step` on states of values, frames as in `State`
  if (s.t === 'reduce') {
    const { a, b, k } = s;
    if (a === LEAF) return dispatch(stem(b), k);
    if (isStem(a)) return dispatch(fork(L[a], b), k);
    const u = L[a], y = R[a];
    if (u === LEAF) return dispatch(y, k);
    if (isStem(u)) return reduce(y, b, [{ t: 'caa', fn: L[u], arg: b }, ...k]);
    if (b === LEAF) return dispatch(L[u], k);
    if (isStem(b)) return reduce(R[u], L[b], k);
    return reduce(y, L[b], [{ t: 'applyTo', arg: R[b] }, ...k]);
  }
  if (!s.k.length) return null;
  const [f, ...k] = s.k;
  return f.t === 'applyTo' ? reduce(s.r, f.arg, k) : reduce(f.fn, f.arg, [{ t: 'applyTo', arg: s.r }, ...k]);
}
const key = (s) => JSON.stringify(s);
let seed = 1;
const rnd = () => (seed = (seed * 1103515245 + 12345) % 2147483648) / 2147483648;
const randomValue = (d) => d === 0 || rnd() < 0.3 ? LEAF : rnd() < 0.5 ? stem(randomValue(d - 1)) : fork(randomValue(d - 1), randomValue(d - 1));
function crossCheck(lhs, rhs, ℓ) {
  for (let t = 0; t < 8; t++) {
    const vals = new Map(), σ = (i) => { if (!vals.has(i)) vals.set(i, randomValue(4)); return vals.get(i); };
    let s = mapState(lhs, (x) => subst(x, σ));
    for (let n = 0; n < ℓ; n++) { s = stepC(s); if (!s) throw new Error('halted early'); }
    if (key(s) !== key(mapState(rhs, (x) => subst(x, σ)))) throw new Error('instance disagrees');
  }
}

// ---- Lean
const usedNames = new Set();
const leanName = (sym) => {
  let n = sym.replace(/^:/, 'p_').replace(/[^A-Za-z0-9_]/g, '_'), m = n;
  for (let i = 2; usedNames.has(m); i++) m = `${n}_${i}`;
  usedNames.add(m); return m;
};
// Values are `def`s, children first — but only the nodes a rule (or an `_out`) mentions, the nodes
// two parents share, and every MAX_INLINE-th level of a long chain: any other node is written
// inline in its one parent. One `def` per node (as Cpp/dag2lean.mjs does) is ~13 times as many
// commands for the whole library, and Lean's frontend overflows its stack past ~12,000 commands
// in one file. Every definition is `noncomputable` (nothing here is run but by the kernel), and
// constructors are written `Value.fork`: code generation and dot-name resolution were most of the
// time a file of big literals took.
const MAX_INLINE = 32;
const direct = new Set();
const markSV = (x) => { if (x.t === 'lit') { if (x.v !== LEAF) direct.add(x.v); } else if (x.t === 'stem') markSV(x.u); else if (x.t === 'fork') { markSV(x.u); markSV(x.v); } };
const mark = (s) => mapState(s, (x) => { markSV(x); return x; });
const kidsOf = (y) => isStem(y) ? [L[y]] : [L[y], R[y]];
const valueDefs = [], valueName = new Map();
function nameValues() {
  const indeg = new Map(), post = [], seen = new Set();
  for (const r of direct) { // postorder, with a stack of our own: a long list is deeper than JavaScript's
    const todo = [[r, false]];
    while (todo.length) {
      const [y, done] = todo.pop();
      if (done) { post.push(y); continue; }
      if (y === LEAF || seen.has(y)) continue;
      seen.add(y); todo.push([y, true]);
      for (const c of kidsOf(y)) { indeg.set(c, (indeg.get(c) ?? 0) + 1); todo.push([c, false]); }
    }
  }
  const depth = new Map(), text = new Map();
  const ref = (c) => c === LEAF ? 'Value.leaf' : valueName.get(c) ?? `(${text.get(c)})`;
  for (const y of post) {
    text.set(y, isStem(y) ? `Value.stem ${ref(L[y])}` : `Value.fork ${ref(L[y])} ${ref(R[y])}`);
    const d = 1 + Math.max(0, ...kidsOf(y).map((c) => depth.get(c) ?? 0));
    if (direct.has(y) || indeg.get(y) > 1 || d > MAX_INLINE) {
      const name = `t${valueName.size + 1}`;
      valueName.set(y, name); valueDefs.push(`noncomputable def ${name} : Value := ${text.get(y)}`); depth.set(y, 0);
    } else depth.set(y, d);
  }
}
const v = (x) => x === LEAF ? '.leaf' : valueName.get(x);
const roots = new Map(); // value → its symbol's def name and block, for readability
let renderAt = 0;        // the block being rendered: a symbol's name is used only after its def
// A pattern nested deeper than MAX_INLINE is cut into `noncomputable def s<k> : SValue`s, put
// before the block that uses them (`auxDefs`): deeply nested terms parse slowly.
const auxDefs = [];
let auxCount = 0;
function svd(x) { // [text, nesting depth]
  switch (x.t) {
    case 'lit': { const r = roots.get(x.v); return [`(.lit ${r && r.at < renderAt ? r.name : v(x.v)})`, 1]; }
    case 'var': return [`(.var ${x.i})`, 1];
    case 'stem': { const [u, d] = svd(x.u); return cut(`(.stem ${u})`, d + 1); }
    case 'fork': { const [u, d] = svd(x.u), [w, e] = svd(x.v); return cut(`(.fork ${u} ${w})`, Math.max(d, e) + 1); }
  }
}
function cut(text, d) {
  if (d <= MAX_INLINE) return [text, d];
  const name = `s${++auxCount}`;
  auxDefs.push(`noncomputable def ${name} : SValue := ${text}`);
  return [name, 1];
}
const sv = (x) => svd(x)[0];
const fr = (f) => f.t === 'applyTo' ? `.applyTo ${sv(f.arg)}` : `.computeAndApply ${sv(f.fn)} ${sv(f.arg)}`;
const st = (s) => `.${s.t} ${s.t === 'reduce' ? `${sv(s.a)} ${sv(s.b)}` : sv(s.r)} [${s.k.map(fr).join(', ')}]`;
const litNodes = (s) => { // distinct nodes of the lits a state embeds
  const seen = new Set(), walk = (x) => {
    const todo = [x];
    while (todo.length) { const y = todo.pop(); if (y === LEAF || seen.has(y)) continue; seen.add(y); todo.push(L[y]); if (!isStem(y)) todo.push(R[y]); }
  };
  mapState(s, function g(x) { if (x.t === 'lit') walk(x.v); else if (x.t === 'stem') g(x.u); else if (x.t === 'fork') { g(x.u); g(x.v); } return x; });
  return seen.size;
};
const svSize = (x) => x.t === 'stem' ? 1 + svSize(x.u) : x.t === 'fork' ? 1 + svSize(x.u) + svSize(x.v) : 1;
const stSize = (s) => { let n = 0; mapState(s, (x) => { n += svSize(x); return x; }); return n; };

const blocks = [], all = [];
const plural = (n, w) => `${n} ${w}${n === 1 ? '' : 's'}`;
// Blocks are rendered once every value is named (`nameValues`).
function emitRule(name, doc, lhs, res, lhsText) {
  crossCheck(lhs, res.s, res.n); mark(lhs); mark(res.s);
  blocks.push(() => `/-- ${doc}: ${plural(res.red, 'reduce step')}, ${plural(res.n, 'transition')}. -/
noncomputable def ${name} : Rule := ⟨${lhsText ?? st(lhs)}, ${st(res.s)}⟩
theorem ${name}_check : checkRule ${name} ${res.n} = true := ${opt.rfl ? 'decide_eq_true (by kernel_rfl)' : 'by decide +kernel'}`);
  all.push(`.one ${name}_check`);
  if (opt.stats) console.error([name, res.n, res.red, litNodes(lhs) + litNodes(res.s), stSize(res.s)].join('\t'));
}

const SHAPE = ['△', '△ _', '△ _ _'];
function derive(sym, arity, split) {
  const f = env.get(sym);
  if (f === undefined) throw new Error(`no symbol ${sym}`);
  const base = leanName(sym);
  if (!roots.has(f) && f !== LEAF) {
    direct.add(f); roots.set(f, { name: `${base}V`, at: blocks.length });
    blocks.push(() => `/-- \`${sym}\` -/\nnoncomputable def ${base}V : Value := ${v(f)}`);
  }
  let fn = lit(f), fnText = `(.lit ${roots.get(f)?.name ?? '.leaf'})`;
  if (!opt.all && arity >= 2) {
    const xs = [...Array(arity).keys()];
    const lhs = reduce(fn, svar(0), xs.slice(1).map((i) => ({ t: 'applyTo', arg: svar(i) })));
    const res = run(lhs);
    emitRule(`${base}_app`, `\`${sym}${xs.map((j) => ` x${j}`).join('')}\`, the whole application ⟶ ${res.stop.t === 'halt' ? 'a value' : `a ${res.stop.where} split on \`x${res.stop.i}\``}`, lhs, res,
      `.reduce ${fnText} (.var 0) [${xs.slice(1).map((i) => `.applyTo (.var ${i})`).join(', ')}]`);
  }
  for (let i = 0; i < arity && all.length < opt.max; i++) {
    const lhs = reduce(fn, svar(i), []);
    const lhsText = `.reduce ${fnText} (.var ${i}) []`;
    const res = run(lhs);
    const app = `\`${sym}${[...Array(i + 1).keys()].map((j) => ` x${j}`).join('')}\``;
    const name = `${base}_${i + 1}`;
    if (res.stop.t === 'split' && res.stop.where === 'arg' && split) {
      // A RuleTree of one split: each leaf's rule runs from the refined left side.
      const j = stateMaxVar(lhs) + 1;
      const pats = [lit(LEAF), sstem(svar(j)), sfork(svar(j), svar(j + 1))];
      const leaves = pats.map((p) => {
        const l2 = mapState(lhs, (x) => inst(x, res.stop.i, p)), r2 = run(l2);
        crossCheck(l2, r2.s, r2.n); mark(lhs); mark(r2.s);
        return r2;
      }).map((r2) => () => `(.leaf (${st(r2.s)}) ${r2.n})`);
      blocks.push(() => `/-- ${app}, by the shape of \`x${res.stop.i}\` (${SHAPE.join(', ')}). -/
noncomputable def ${name}_tree : RuleTree := .split ${res.stop.i} ${j}\n  ${leaves.map((f) => f()).join('\n  ')}
theorem ${name}_tree_check : checkRules (${name}_tree.rules (${lhsText})) = true := by decide +kernel`);
      all.push(`⟨_, ${name}_tree_check⟩`);
      return;
    }
    if (res.n >= opt.min) emitRule(name, `${app} ⟶ ${res.stop.t === 'halt' ? 'a value' : `a ${res.stop.where} split on \`x${res.stop.i}\``}`, lhs, res, lhsText);
    if (res.stop.t !== 'halt') return;
    // The next stage starts from the value this one returns, named for the reader.
    const out = `${name}_out`;
    const r = res.s.r; markSV(r);
    blocks.push(() => `/-- What ${app} returns. -/\nnoncomputable def ${out} : SValue := ${sv(r)}`);
    fn = res.s.r; fnText = out;
  }
}

const todo = opt.all ? order.filter((s) => !s.startsWith(':test') && !s.startsWith(':source')).map((s) => [s, opt.all, false])
  : specs.map((s) => { const [sym, n, sp] = s.split('/'); return [sym, +n, sp === 'split']; });
const seenSym = new Set();
for (const [sym, n, sp] of todo) {
  if (all.length >= opt.max) break;
  if (seenSym.has(env.get(sym)) && opt.all) continue; // aliases: one copy of each value
  seenSym.add(env.get(sym));
  try { derive(sym, n, sp); } catch (e) { console.error(`${sym}: ${e.message}`); }
}

nameValues();
const shown = file.startsWith('/') ? file.split('/').slice(-3).join('/') : file;
console.log(`-- Generated by gen-rules.mjs from ${shown}; do not edit.
--   node Rules/gen-rules.mjs ${args.map((a) => a === file ? shown : a).join(' ')}
import TreeCalculus.RuleJets${opt.rfl ? '\nimport Rules.KernelRfl' : ''}

namespace ${opt.ns}
open TreeCalculus

${valueDefs.join('\n')}

${blocks.map((b, i) => { renderAt = i; const text = b(); return [...auxDefs.splice(0), text].join('\n'); }).join('\n\n')}

/-- Every rule above, checked. -/
noncomputable def all : List Checked := [
  ${all.join(',\n  ')}]

end ${opt.ns}`);
