import { Evaluator, raise } from "../common.mjs";

// Strategy: Like [eager-value-mem] but lazy. Never frees memory.

type Tree = number; // index into arrays of [Ctx]
type Ctx = {
  type: Int32Array, // 0 = leaf, 1 = stem, 2 = fork, 3 = thunk
  u: Int32Array, // (left) child for stems, forks or thunks
  v: Int32Array, // right child for forks or thunks
  free_from: number,
};

const make_evaluator: () => Evaluator<Tree> = () => {
  let size = 1024 * 1024;
  const expand = () => {
    size = size * 2;
    const u = new Int32Array(size);
    const v = new Int32Array(size);
    const type = new Int32Array(size);
    u.set(ctx.u);
    v.set(ctx.v);
    type.set(ctx.type);
    ctx.u = u;
    ctx.v = v;
    ctx.type = type;
  };
  const ctx: Ctx = {
    type: new Int32Array(size),
    u: new Int32Array(size),
    v: new Int32Array(size),
    free_from: 1 // implicitly make index 0 the (one and only) leaf node
  };
  const alloc = (): Tree => {
    if (ctx.free_from === size) expand();
    return ctx.free_from++;
  };
  const alloc_stem = (u: Tree, target?: Tree): Tree => {
    const res = target ?? alloc();
    ctx.type[res] = 1;
    ctx.u[res] = u;
    return res;
  };
  const alloc_fork = (u: Tree, v: Tree, target?: Tree): Tree => {
    const res = target ?? alloc();
    ctx.type[res] = 2;
    ctx.u[res] = u;
    ctx.v[res] = v;
    return res;
  };
  const apply = (a: Tree, b: Tree, target?: Tree): Tree => {
    const res = target ?? alloc();
    ctx.type[res] = 3;
    ctx.u[res] = a;
    ctx.v[res] = b;
    return res;
  };
  const reduce_one = function* (app: Tree): Generator<Tree> {
    while (ctx.type[app] === 3) {
      const a = ctx.u[app];
      if (ctx.type[a] === 3) yield a;
      const b = ctx.v[app];
      switch (ctx.type[a]) {
        case 0:
          alloc_stem(b, app);
          break;
        case 1:
          alloc_fork(ctx.u[a], b, app);
          break;
        case 2:
          debug.num_steps++;
          const u = ctx.u[a];
          if (ctx.type[u] === 3) yield u;
          switch (ctx.type[u]) {
            case 0:
              const tmp = ctx.v[a];
              if (ctx.type[tmp] === 3) yield tmp;
              ctx.type[app] = ctx.type[tmp];
              ctx.u[app] = ctx.u[tmp];
              ctx.v[app] = ctx.v[tmp];
              break;
            case 1:
              apply(apply(ctx.u[u], b), apply(ctx.v[a], b), app);
              break;
            case 2:
              if (ctx.type[b] === 3) yield b;
              switch (ctx.type[b]) {
                case 0:
                  const tmp = ctx.u[u];
                  if (ctx.type[tmp] === 3) yield tmp;
                  ctx.type[app] = ctx.type[tmp];
                  ctx.u[app] = ctx.u[tmp];
                  ctx.v[app] = ctx.v[tmp];
                  break;
                case 1:
                  apply(ctx.v[u], ctx.u[b], app);
                  break;
                case 2:
                  apply(apply(ctx.v[a], ctx.u[b]), ctx.v[b], app);
                  break;
                default: return raise(`invariant violation: type ${ctx.type[b]} at index ${b} not 0, 1 or 2`);
              }
              break;
            default: return raise(`invariant violation: type ${ctx.type[u]} at index ${u} not 0, 1 or 2`);
          }
          break;
        default: return raise(`invariant violation: type ${ctx.type[a]} at index ${a} not 0, 1 or 2`);
      }
    }
  };
  const force_root = (t: Tree): void => {
    const force = [reduce_one(t)];
    while (force.length > 0) {
      const next = force[force.length - 1].next();
      if (next.done) {
        force.pop();
      } else {
        force.push(reduce_one(next.value));
      }
    }
  };
  const evaluator: Evaluator<Tree> = {
    // construct
    leaf: 0,
    stem: alloc_stem,
    fork: alloc_fork,
    // eval
    apply,
    // destruct
    triage: (on_leaf, on_stem, on_fork) => x => {
      if (ctx.type[x] === 3) force_root(x);
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

const debug = { num_steps: 0 };
export { debug };
export default make_evaluator;
