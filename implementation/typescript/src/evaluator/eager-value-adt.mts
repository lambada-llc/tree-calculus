import { Evaluator } from "../common.mjs";

// Strategy: Represent only values as kind of an algebraic data type and define [apply] recursively.

type Tree =
  [] | // = Leaf
  [Tree] | // = Stem u
  [Tree, Tree]; // = Fork u v

function apply(a: Tree, b: Tree): Tree {
  switch (a.length) {
    case 0: return [b];
    case 1: return [a[0], b];
    case 2:
      debug.num_steps++;
      switch (a[0].length) {
        case 0: return a[1];
        case 1: return apply(apply(a[0][0], b), apply(a[1], b));
        case 2:
          switch (b.length) {
            case 0: return a[0][0];
            case 1: return apply(a[0][1], b[0]);
            case 2: return apply(apply(a[1], b[0]), b[1]);
          }
      }
  }
}

const evaluator: Evaluator<Tree> = {
  // construct
  leaf: [],
  stem: u => [u],
  fork: (u, v) => [u, v],
  // eval
  apply,
  // destruct
  triage: (on_leaf, on_stem, on_fork) => x => {
    switch (x.length) {
      case 0: return on_leaf();
      case 1: return on_stem(x[0]);
      case 2: return on_fork(x[0], x[1]);
    }
  }
};

const debug = { num_steps: 0 };
export { debug };
export default evaluator;
