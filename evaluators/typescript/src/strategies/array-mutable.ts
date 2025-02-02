import { Evaluator } from "../common";

export type Tree = Tree[]; // = △ <array entries in reverse order>

const reduceOne = (todo: Tree[]): void => {
  const f = todo.pop();
  if (f.length < 3) return;
  todo.push(f);
  const a = f.pop(), b = f.pop(), c = f.pop();
  if (a.length === 0) f.push(...b); // leaf
  else if (a.length === 1) { // stem
    const newPotRedex = [c, ...b];
    f.push(newPotRedex, c, ...a[0]);
    todo.push(newPotRedex);
  }
  else if (a.length === 2) // fork
    if (c.length === 0) f.push(...a[1]); // leaf
    else if (c.length === 1) f.push(c[0], ...a[0]); // stem
    else if (c.length === 2) f.push(c[0], c[1], ...b); // fork
};

function reduce(expression: Tree): Tree { // assumes all but top level of expression is already fully reduced!
  const todo = [expression];
  while (todo.length)
    reduceOne(todo);
  return expression;
}

const evaluator: Evaluator<Tree> = {
  // construct
  leaf: [],
  stem: u => [u],
  fork: (u, v) => [v, u],
  // eval
  apply: (a, b) => reduce([b, ...a]),
  // destruct
  triage: (on_leaf, on_stem, on_fork) => x => {
    switch (x.length) {
      case 0: return on_leaf;
      case 1: return on_stem(x[0]);
      case 2: return on_fork(x[1], x[0]);
      default: throw new Error('not a value/binary tree');
    }
  }
};

export default evaluator;
