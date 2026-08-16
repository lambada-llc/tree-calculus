#!/usr/bin/env node
// One-rewrite-at-a-time reduction, fast enough to log every step of a long
// reduction: subterms are shared and carry cached node counts and a
// normal-form flag, so the whole-term size is cheap at every step.
//
// The default order is root-first (leftmost-outermost), which reaches the
// normal form of weakly normalizing terms -- at the price of duplicating
// pending work, which can grow terms an eager evaluator keeps small. --eager
// switches to eager order (arguments are values before a rule fires), where a
// zipper moves between redexes and a step is O(1) amortized.
//
// stdin: whitespace-separated ternary terms, applied left-associatively.
// Each step prints "<step> <nodes>"; --terms appends the term in △ notation,
// --limit N stops after N steps, --peek applies the rule-2 shortcuts that
// avoid duplicating the argument, --fuse [depth] (implies --peek) additionally
// compresses transient size spikes: after a fire that grew the head node, it
// keeps firing at that head -- up to depth (default 8) -- while the node is
// still above its pre-fire size, recording only the composite. Every recorded
// term is a term of the plain sequence, so the trace's peak is what a stepper
// with a depth-deep fused rule table would materialize.
//
// --shrink-eager is a different strategy over the plain rules (it composes
// with neither --peek nor --fuse): only rule 2 with a non-leaf argument can
// grow a term, and every other fire shrinks it without duplicating anything,
// so firing shrink redexes first -- biggest drop first, the growing rule 2
// leftmost-outermost only when nothing shrinks -- keeps every intermediate at
// most as large as under root-first order (a residual argument makes that
// pointwise). Same normal forms; each step is one canonical rewrite. Its
// leftmost pick is not proven normalizing, so pair it with --limit if in
// doubt (min-growth picks provably diverge on the __wn family).

import { readFileSync } from 'node:fs';

// A term is △ applied to its elements: { kids, n: nodes, nf: normal form }.
const LEAF = { kids: [], n: 1, nf: true };
const mk = kids => {
  let n = 1, nf = kids.length <= 2;
  for (const k of kids) { n += k.n; nf &&= k.nf; }
  return kids.length ? { kids, n, nf } : LEAF;
};

const patch = (node, idx, child) => {
  const kids = node.kids.slice();
  kids[idx] = child;
  return mk(kids);
};

// --peek: the three rule-2 shortcuts that avoid duplicating c into
// (x c) (b c) -- x = K yields c outright (b c is never built), and an
// eliminator (K f) as x or b collapses its application to f (apK).
// Contractions of the plain sequence: same normal forms, fewer steps.
const apK = (f, a) =>
  f.kids.length === 2 && f.kids[0].kids.length === 0 ? f.kids[1] : mk([...f.kids, a]);

// The rewrite at a spine head. Rules 1 and 2 never inspect c, so c may still
// hold pending work under root-first order; rule 3 requires its shape.
const fire = ([a, b, c, ...w]) => {
  if (a.kids.length === 0) return [...b.kids, ...w];                    // rule 1
  if (a.kids.length === 1) {                                            // rule 2
    const [x] = a.kids;
    if (!peek) return [...x.kids, c, mk([...b.kids, c]), ...w];
    if (x.kids.length === 1 && x.kids[0].kids.length === 0) return [...c.kids, ...w]; // x = K
    return [...apK(x, c).kids, apK(b, c), ...w];
  }
  const [x, y] = a.kids;                                                // rule 3, by shape of c
  if (c.kids.length === 0) return [...x.kids, ...w];
  if (c.kids.length === 1) return [...y.kids, c.kids[0], ...w];
  return [...b.kids, c.kids[0], c.kids[1], ...w];
};

const fireable = k =>
  k.length >= 3 && k[0].kids.length < 3 && (k[0].kids.length < 2 || k[2].kids.length < 3);

// One recorded rewrite: a single fire, or with --fuse the transient-skipping
// composite (see header).
const fireChain = k => {
  let out = fire(k);
  if (fuse) {
    const size = ks => ks.reduce((s, x) => s + x.n, 1);
    const start = size(k);
    for (let n = 1; n < fuse && size(out) > start && fireable(out); n++) out = fire(out);
  }
  return out;
};

const parseTernary = s => {
  const frames = [{ need: 1, kids: [] }];
  for (const ch of s) {
    frames.push({ need: +ch, kids: [] });
    while (frames.length > 1 && frames.at(-1).need === frames.at(-1).kids.length) {
      const f = frames.pop();
      frames.at(-1).kids.push(mk(f.kids));
    }
  }
  return frames[0].kids[0];
};

const fmt = root => {
  const out = [], stack = [root];
  while (stack.length) {
    const it = stack.pop();
    if (typeof it === 'string') { out.push(it); continue; }
    out.push('△');
    for (let i = it.kids.length - 1; i >= 0; i--) {
      const k = it.kids[i];
      if (k.kids.length === 0) stack.push(' △');
      else { stack.push(')', k, ' ('); }
    }
  }
  return out.join('');
};

const output = (() => {
  const buf = [];
  return (line, flush) => {
    if (line !== null) buf.push(line);
    if (buf.length === 65536 || (flush && buf.length)) {
      process.stdout.write(buf.join('\n') + '\n');
      buf.length = 0;
    }
  };
})();

// Root-first: fire at the outermost spine, descending only where a rule's
// shape dispatch demands a value (the function, and the argument of rule 3).
// Below the outermost redex, terms are values normalized left to right.
const stepNormal = root => {
  const path = [];
  let t = root;
  for (;;) {
    const k = t.kids;
    if (k.length >= 3) {
      if (k[0].kids.length >= 3) { path.push({ node: t, idx: 0 }); t = k[0]; continue; }
      if (k[0].kids.length === 2 && k[2].kids.length >= 3) { path.push({ node: t, idx: 2 }); t = k[2]; continue; }
      t = mk(fireChain(k));
      break;
    }
    const j = k.findIndex(x => !x.nf);
    if (j < 0) return null;                     // normal form
    path.push({ node: t, idx: j }); t = k[j];
  }
  for (let j = path.length - 1; j >= 0; j--) t = patch(path[j].node, path[j].idx, t);
  return t;
};

// One canonical rewrite, shrink-eager: the biggest-drop shrink redex anywhere,
// else the leftmost-outermost growing rule 2.
const shrinkDrop = k => {
  const [a, b, c] = k;
  if (a.kids.length === 0) return 2 + c.n;                              // rule 1
  if (a.kids.length === 1) return 1;                                    // rule 2, leaf c
  const [x, y] = a.kids;                                                // rule 3, by shape of c
  if (c.kids.length === 0) return 3 + y.n + b.n;
  if (c.kids.length === 1) return 3 + x.n + b.n;
  return 3 + x.n + y.n;
};
const stepShrinkEager = root => {
  let bestShrink = null, firstGrow = null;
  const walk = (t, path) => {
    if (t.nf) return;
    const k = t.kids;
    if (k.length >= 3 && k[0].kids.length < 3 && (k[0].kids.length < 2 || k[2].kids.length < 3)) {
      if (k[0].kids.length !== 1 || k[2].kids.length === 0) {
        const drop = shrinkDrop(k);
        if (!bestShrink || drop > bestShrink.drop) bestShrink = { path, drop };
      } else if (!firstGrow) firstGrow = { path };
    }
    for (let i = 0; i < k.length; i++) walk(k[i], path.concat(i));
  };
  walk(root, []);
  const target = bestShrink ?? firstGrow;
  if (!target) return null;
  const spine = [];
  let t = root;
  for (const i of target.path) { spine.push({ node: t, idx: i }); t = t.kids[i]; }
  t = mk(fire(t.kids));
  for (let j = spine.length - 1; j >= 0; j--) t = patch(spine[j].node, spine[j].idx, t);
  return t;
};

const traceNormal = (root, limit, terms, step = stepNormal) => {
  let i = 0;
  output(terms ? `${i} ${root.n} ${fmt(root)}` : `${i} ${root.n}`);
  while (i < limit && (root = step(root)) !== null) {
    i++;
    output(terms ? `${i} ${root.n} ${fmt(root)}` : `${i} ${root.n}`);
  }
  output(null, true);
};

const traceEager = (root, limit, terms) => {
  const frames = [];                    // { node, idx }: we are inside node.kids[idx]
  let focus = root, ctx = 0, i = 0;     // invariant: whole-term size = ctx + focus.n
  const rebuild = () => {
    let t = focus;
    for (let j = frames.length - 1; j >= 0; j--) t = patch(frames[j].node, frames[j].idx, t);
    return t;
  };
  const line = () =>
    output(terms ? `${i} ${ctx + focus.n} ${fmt(rebuild())}` : `${i} ${ctx + focus.n}`);
  line();
  while (i < limit) {
    while (focus.nf && frames.length) {         // done below here: rebuild upward
      const { node, idx } = frames.pop();
      ctx -= node.n - node.kids[idx].n;
      focus = patch(node, idx, focus);
    }
    if (focus.nf) break;
    for (;;) {                                  // descend to the leftmost-innermost redex
      const j = focus.kids.findIndex(k => !k.nf);
      if (j < 0) break;
      frames.push({ node: focus, idx: j });
      ctx += focus.n - focus.kids[j].n;
      focus = focus.kids[j];
    }
    focus = mk(fireChain(focus.kids));          // all kids normal, so arity >= 3
    i++; line();
  }
  output(null, true);
};

const args = process.argv.slice(2);
const li = args.indexOf('--limit');
const limit = li >= 0 ? Number(args[li + 1]) : Infinity;
const fi = args.indexOf('--fuse');
const fuse = fi >= 0 ? (Number(args[fi + 1]) || 8) : 0;
const peek = fuse > 0 || args.includes('--peek');
const toks = readFileSync(0, 'utf8').split(/\s+/).filter(Boolean);
let t = null;
for (const s of toks) {
  const u = parseTernary(s);
  t = t ? mk([...t.kids, u]) : u;
}
if (args.includes('--eager')) traceEager(t, limit, args.includes('--terms'));
else traceNormal(t, limit, args.includes('--terms'),
  args.includes('--shrink-eager') ? stepShrinkEager : stepNormal);
