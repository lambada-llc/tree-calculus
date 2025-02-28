import { Evaluator } from "../common";

// Like eager version of this strategy, but with ability to delay application.
// As the benchmarks show, this has the expected benefits for programs that do
// needless work, but comes with a bit of overhead otherwise.

type Value =
  [] | // = Leaf
  [Expr] | // = Stem u
  [Expr, Expr]; // = Fork u v
type Expr =
  Value |
  [Expr, Expr, null]; // a b

const force_head = (e: Expr): Value => {
  if (e.length === 3) {
    e.pop();
    const b = e.pop()!;
    const a = e.pop()!;
    e.push(...force_apply(a, b));
  }
  return e as any;
}

function force_apply(ae: Expr, be: Expr): Value {
  const a = force_head(ae);
  switch (a.length) {
    case 0: return [be];
    case 1: return [a[0], be];
    case 2:
      const a0 = force_head(a[0]);
      switch (a0.length) {
        case 0: return force_head(a[1]);
        case 1: return force_apply(force_apply(a0[0], be), apply(a[1], be));
        case 2:
          const b = force_head(be);
          switch (b.length) {
            case 0: return force_head(a0[0]);
            case 1: return force_apply(a0[1], b[0]);
            case 2: return force_apply(force_apply(a[1], b[0]), b[1]);
          }
      }
  }
}

const apply = (a: Expr, b: Expr): Expr => [a, b, null];

const evaluator: Evaluator<Expr> = {
  // construct
  leaf: [],
  stem: u => [u],
  fork: (u, v) => [u, v],
  // eval
  apply,
  // destruct
  triage: (on_leaf, on_stem, on_fork) => e => {
    const x = force_head(e);
    switch (x.length) {
      case 0: return on_leaf();
      case 1: return on_stem(x[0]);
      case 2: return on_fork(x[0], x[1]);
    }
  }
};

export default evaluator;
