import { Evaluator, raise } from "../common";

// Strategy: Like [eager-value-adt] but memory layout and management is explicit. Never frees memory.

type Tree = number; // index into arrays of [Ctx]
type Ctx = {
  type: Int32Array, // 0 = leaf, 1 = stem, 2 = fork
  u: Int32Array, // (left) child for stems or forks
  v: Int32Array, // right child for forks
  free_from: number,
};

const make_evaluator: () => Evaluator<Tree> = () => {
  // TODO: auto-resize
  const size = 1024 * 1024 * 16;
  const ctx: Ctx = {
    type: new Int32Array(size),
    u: new Int32Array(size),
    v: new Int32Array(size),
    free_from: 1 // implicitly make index 0 the (one and only) leaf node
  };
  const allocInner = (): Tree => {
    if (ctx.free_from === size) return raise('out of memory');
    return ctx.free_from++;
  };
  const allocStem = (u: Tree): Tree => {
    const res = allocInner();
    ctx.type[res] = 1;
    ctx.u[res] = u;
    return res;
  };
  const allocFork = (u: Tree, v: Tree): Tree => {
    const res = allocInner();
    ctx.type[res] = 2;
    ctx.u[res] = u;
    ctx.v[res] = v;
    return res;
  };
  const apply = (a: Tree, b: Tree): Tree => {
    switch (ctx.type[a]) {
      case 0: return allocStem(b);
      case 1: return allocFork(ctx.u[a], b);
      case 2:
        const u = ctx.u[a];
        switch (ctx.type[u]) {
          case 0: return ctx.v[a];
          case 1: return apply(apply(ctx.u[u], b), apply(ctx.v[a], b));
          case 2:
            switch (ctx.type[b]) {
              case 0: return ctx.u[u];
              case 1: return apply(ctx.v[u], ctx.u[b]);
              case 2: return apply(apply(ctx.v[a], ctx.u[b]), ctx.v[b]);
              default: return raise(`invariant violation: type ${ctx.type[b]} at index ${b} not 0, 1 or 2`);
            }
          default: return raise(`invariant violation: type ${ctx.type[u]} at index ${u} not 0, 1 or 2`);
        }
      default: return raise(`invariant violation: type ${ctx.type[a]} at index ${a} not 0, 1 or 2`);
    }
  };
  const evaluator: Evaluator<Tree> = {
    // construct
    leaf: 0,
    stem: allocStem,
    fork: allocFork,
    // eval
    apply,
    // destruct
    triage: (on_leaf, on_stem, on_fork) => x => {
      switch (ctx.type[x]) {
        case 0: return on_leaf();
        case 1: return on_stem(ctx.u[x]);
        case 2: return on_fork(ctx.u[x], ctx.v[x]);
      }
      return raise(`invariant violation: type ${ctx.type[x]} at index ${x} not 0, 1 or 2`)
    }
  };
  return evaluator;
};

export default make_evaluator;
