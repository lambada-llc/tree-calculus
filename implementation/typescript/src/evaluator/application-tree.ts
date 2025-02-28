import { Evaluator, raise } from "../common";

// Disclaimer: It may seem like a good idea to represent expressions as binary trees
// where leaves are △ and inner nodes are function application. This implementation
// aims to demonstrate that this is not a good idea, at least compared to the other
// strategies presented here.
//
// Problems:
// * There is no natural distinction between redex and value. Just to check whether a
//   subtree is reducible requires traversing a bunch of pointers. Note that finding
//   a △ applied to three (or more) parameters is not sufficient alone. The first
//   argument needs to be reduced enough to distinguish whether it's a leaf, stem or
//   fork. If it's a fork, the same is necessary for the third argument. Below, the
//   strategy around this is to enforce an eager evaluation order. A lazy strategy
//   would need to traverse even more pointers -- or do something like tagging
//   subtrees as values. At that point, one may as well make leafs, stems and forks
//   first-class constructs.
// * When having reduced a subtree, it's annoying to track who had a reference to
//   the unreduced subtree and hence should be updated. It might be the left/right
//   side of some application node, it might be the original "apply" caller. Note
//   how adding another level of indirection (cells/references/pointers) increases
//   the amount of pointer traversing even more, while not straightforwardly solving
//   the problem either: Reduction of "△ △ a b" to "a" must effectively *unify* the
//   pointers representing those two expressions. After all, "a" might reduce further
//   and we'll want to update anyone who originally referred to either expression.
//   The strategy below gets around this by being eager: "a" is already a value by
//   the time we encounter the reduction of "△ △ a b".

type Tree = undefined // = △
  | [Tree, Tree]; // = a b

const triage = <T>(on_leaf: () => T, on_stem: (u: Tree) => T, on_fork: (u: Tree, v: Tree) => T) => (x: Tree) => {
  if (x === undefined) return on_leaf();
  if (x[0] === undefined) return on_stem(x[1]);
  if (x[0][0] === undefined) return on_fork(x[0][1], x[1]);
  throw new Error('not a value/binary tree');
};

type Spine = { app: [Tree, Tree], put: (t: Tree) => void }[];

const reduceOne = (todo: Spine[]): void => {
  const spine = todo.pop()!;
  if (spine.length === 0) return;
  while (true) {
    const app = spine[spine.length - 1].app;
    if (app[0] === undefined) break;
    spine.push({ app: app[0], put: t => app[0] = t });
  }
  if (spine.length < 3) return;
  todo.push(spine);
  // Binary tree of applications
  //       z
  //      / \
  //     y
  //    / \
  //   x
  //  / \
  // △
  const x = spine.pop()!, y = spine.pop()!, z = spine.pop()!;
  const putApp = (app: [Tree, Tree]) => {
    z.put(app);
    spine.push({ app, put: z.put });
  };
  if (x.app[1] === undefined) z.put(y.app[1]); // leaf
  else {
    const [x1, x2] = x.app[1];
    if (x1 === undefined) { // stem
      const app1: Tree = [x2, z.app[1]];
      const app2: Tree = [y.app[1], z.app[1]];
      const app: Tree = [app1, app2];
      putApp(app);
      todo.push([{ app: app2, put: t => app[1] = t }]);
    }
    else { // fork
      const [x1a, x1b] = x1;
      if (x1a !== undefined) raise('invariant violation');
      triage(
        () => z.put(x1b),
        (u) => putApp([x2, u]),
        (u, v) => putApp([[y.app[1], u], v]),
      )(z.app[1]);
    }
  }
};

function apply(a: Tree, b: Tree): Tree {
  let app: Tree = [a, b];
  const todo: Spine[] = [[{ app, put: t => app = t }]];
  while (todo.length)
    reduceOne(todo);
  return app;
}

const evaluator: Evaluator<Tree> = {
  // construct
  leaf: undefined,
  stem: u => [undefined, u],
  fork: (u, v) => [[undefined, u], v],
  // eval
  apply,
  // destruct
  triage
};

export default evaluator;
