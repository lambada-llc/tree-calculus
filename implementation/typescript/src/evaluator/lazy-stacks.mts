import { Evaluator } from "../common.mjs";

type Tree = Tree[]; // = △ <array entries in reverse order>

const reduce_one = function* (s: Tree[]): Generator<Tree> {
  while (s.length >= 3) {
    debug.num_steps++;
    const x = s.pop()!, y = s.pop()!, z = s.pop()!;
    if (x.length > 2) yield x;
    if (x.length === 0) {
      if (y.length > 2) yield y;
      s.push(...y); // leaf
    }
    else if (x.length === 1) {
      if (x[0].length > 2) yield x[0];
      // [z, ...y] is tricky:
      // - if y is unreduced and we don't force it, we may end up reducing it multiple times
      // - if y is unreduced and we force it, it might end up getting dropped
      // if (y.length > 2) yield y;
      s.push([z, ...y], z, ...x[0]);
    }
    else if (x.length === 2) { // fork
      if (z.length > 2) yield z;
      if (z.length === 0) { // leaf
        if (x[1].length > 2) yield x[1];
        s.push(...x[1]);
      }
      else if (z.length === 1) { // stem
        if (x[0].length > 2) yield x[0];
        s.push(z[0], ...x[0]);
      }
      else if (z.length === 2) { // fork
        if (y.length > 2) yield y;
        s.push(z[0], z[1], ...y);
      }
    }
  }
};

function force_root(expression: Tree): void {
  const force = [reduce_one(expression)];
  while (force.length > 0) {
    const next = force[force.length - 1].next();
    if (next.done) {
      force.pop();
    } else {
      force.push(reduce_one(next.value));
    }
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
