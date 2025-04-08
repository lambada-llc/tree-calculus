import { Evaluator } from "../common.mjs";

type Tree = Tree[]; // = △ <array entries in reverse order>

const reduceOne = (todo: Tree[]): void => {
  const s = todo.pop()!;
  if (s.length < 3) return;
  debug.num_steps++;
  todo.push(s);
  const x = s.pop()!, y = s.pop()!, z = s.pop()!;
  if (x.length === 0) s.push(...y); // leaf
  else if (x.length === 1) { // stem
    const newPotRedex = [z, ...y];
    s.push(newPotRedex, z, ...x[0]);
    todo.push(newPotRedex);
  }
  else if (x.length === 2) // fork
    if (z.length === 0) s.push(...x[1]); // leaf
    else if (z.length === 1) s.push(z[0], ...x[0]); // stem
    else if (z.length === 2) s.push(z[0], z[1], ...y); // fork
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
      case 0: return on_leaf();
      case 1: return on_stem(x[0]);
      case 2: return on_fork(x[1], x[0]);
      default: throw new Error('not a value/binary tree');
    }
  }
};

let debug = { num_steps: 0 };
export { debug };
export default evaluator;
