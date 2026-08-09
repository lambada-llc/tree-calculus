import { Evaluator } from "../common.mjs";

// `lazy-stacks` tuned for the host language. Same rules, same reduction order,
// same reductions in the same sequence -- it reports identical step counts on
// every program tried. Two things differ, both mechanical, together worth a bit
// over 2x:
//
// 1. The pushes are written out per length rather than spread. Forcing reduces a
//    node to a value, so every operand spread in the original holds at most two
//    arguments, and a spread costs a variadic call where a fixed-arity push does
//    not. The loop does one or two per reduction. The exception is rule 2's y,
//    which is not forced, so its common lengths are unrolled and the general
//    case kept.
//
// 2. The generator is gone. It existed to suspend a reduction mid-step while a
//    sub-node is forced and resume it afterwards, at the cost of a generator
//    object per forced node and an iterator result per resumption. Instead, a
//    step that needs a sub-node forced pushes its three operands back, puts the
//    sub-node on the work stack, and re-runs the step once that finishes. The
//    re-run is three pops and a length test, and can happen at most once per
//    force, because forcing leaves the node a value.
//
// Kept apart from `lazy-stacks` rather than folded into it because the two are
// worth reading against each other: one says what the machine does, this one
// says what that costs here.

type Tree = Tree[]; // = △ <array entries in reverse order>

// num_steps is counted where a rewrite happens rather than at the top of the
// loop, so that re-running a step after a force is not counted as a reduction.
function force_root(root: Tree): void {
  const work = [root];
  outer:
  while (work.length > 0) {
    const s = work[work.length - 1];
    while (s.length >= 3) {
      const x = s.pop()!, y = s.pop()!, z = s.pop()!;
      if (x.length > 2) { s.push(z, y, x); work.push(x); continue outer; }
      if (x.length === 0) { // leaf
        if (y.length > 2) { s.push(z, y, x); work.push(y); continue outer; }
        debug.num_steps++;
        if (y.length === 1) s.push(y[0]);
        else if (y.length === 2) s.push(y[0], y[1]);
      }
      else if (x.length === 1) {
        const u = x[0];
        if (u.length > 2) { s.push(z, y, x); work.push(u); continue outer; }
        debug.num_steps++;
        // [z, ...y] is tricky:
        // - if y is unreduced and we don't force it, we may end up reducing it multiple times
        // - if y is unreduced and we force it, it might end up getting dropped
        // if (y.length > 2) { ... force y ... }
        const yz: Tree =
          y.length === 0 ? [z] :
          y.length === 1 ? [z, y[0]] :
          y.length === 2 ? [z, y[0], y[1]] :
          [z, ...y];
        if (u.length === 0) s.push(yz, z);
        else if (u.length === 1) s.push(yz, z, u[0]);
        else s.push(yz, z, u[0], u[1]);
      }
      else { // fork
        if (z.length > 2) { s.push(z, y, x); work.push(z); continue outer; }
        if (z.length === 0) { // leaf
          const v = x[1];
          if (v.length > 2) { s.push(z, y, x); work.push(v); continue outer; }
          debug.num_steps++;
          if (v.length === 1) s.push(v[0]);
          else if (v.length === 2) s.push(v[0], v[1]);
        }
        else if (z.length === 1) { // stem
          const u = x[0];
          if (u.length > 2) { s.push(z, y, x); work.push(u); continue outer; }
          debug.num_steps++;
          if (u.length === 0) s.push(z[0]);
          else if (u.length === 1) s.push(z[0], u[0]);
          else s.push(z[0], u[0], u[1]);
        }
        else { // fork
          if (y.length > 2) { s.push(z, y, x); work.push(y); continue outer; }
          debug.num_steps++;
          if (y.length === 0) s.push(z[0], z[1]);
          else if (y.length === 1) s.push(z[0], z[1], y[0]);
          else s.push(z[0], z[1], y[0], y[1]);
        }
      }
    }
    work.pop();
  }
}

const evaluator: Evaluator<Tree> = {
  // construct
  leaf: [],
  stem: u => [u],
  fork: (u, v) => [v, u],
  // eval
  apply: (a, b) => [b, ...a],
  // destruct
  triage: (on_leaf, on_stem, on_fork) => x => {
    force_root(x);
    switch (x.length) {
      case 0: return on_leaf();
      case 1: return on_stem(x[0]);
      case 2: return on_fork(x[1], x[0]);
      default: throw new Error('not a value/binary tree');
    }
  }
};

const debug = { num_steps: 0 };
export { debug };
export default evaluator;
