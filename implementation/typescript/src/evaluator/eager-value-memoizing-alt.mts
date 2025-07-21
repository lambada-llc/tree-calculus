import { Evaluator } from "../common.mjs";

// Alternative to the memoizing base strategy: Allocate less by representing
// shallow nodes differently.

type Id = number;

type Shallow_node =
  null | // Leaf
  Id | // Stem u
  [Id, Id]; // Fork u v

type Ctx = {
  decode: Shallow_node[]; // map from id to shallow node
  cache: { [a: Id]: { [b: Id]: Id } }; // map from id to map from id to id
};

const make_evaluator: () => Evaluator<Id> = () => {
  const ctx: Ctx = {
    decode: [null], // 0 maps to leaf
    cache: {},
  };
  const alloc = (x: Shallow_node) => ctx.decode.push(x) - 1;
  const apply = (aid: Id, bid: Id): Id => {
    const cache = (ctx.cache[aid] ?? (ctx.cache[aid] = {}));
    if (cache[bid] !== undefined) return cache[bid];
    let result: Id;
    const a = ctx.decode[aid];
    if (a === null) result = alloc(bid);
    else if (typeof a === 'number') result = alloc([a, bid]);
    else {
      const [xid, yid] = a;
      const x = ctx.decode[xid];
      debug.num_steps++;
      if (x === null) result = yid;
      else if (typeof x === 'number') result = apply(apply(x, bid), apply(yid, bid));
      else {
        const b = ctx.decode[bid];
        if (b === null) result = x[0];
        else if (typeof b === 'number') result = apply(x[1], b);
        else result = apply(apply(a[1], b[0]), b[1]);
      }
    }
    cache[bid] = result;
    return result;
  };
  // TODO: this is the spot where one could proactively encode some known trees,
  // so they end up with IDs. Then in [apply] one can add fast paths for them.
  const evaluator: Evaluator<Id> = {
    // construct
    leaf: 0,
    stem: u => apply(0, u),
    fork: (u, v) => apply(apply(0, u), v),
    // eval
    apply,
    // destruct
    triage: (on_leaf, on_stem, on_fork) => xid => {
      const x = ctx.decode[xid];
      if (x === null) return on_leaf();
      else if (typeof x === 'number') return on_stem(x);
      else return on_fork(x[0], x[1]);
    }
  };
  return evaluator;
}

const debug = { num_steps: 0 };
export { debug };
export default make_evaluator;
