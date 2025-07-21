import { Evaluator } from "../common.mjs";

// Strategy: Represent only values, as unique IDs that allow detecting trees of
// specific shape (e.g. functions where a fast implementation is known) and
// memoizing the result of applications.

type Id = number;

type Shallow_node = [] | [Id] | [Id, Id];

type Ctx = {
  decode: Shallow_node[]; // map from id to shallow node
  cache: { [a: Id]: { [b: Id]: Id } }; // map from id to map from id to id
};

const make_evaluator: () => Evaluator<Id> = () => {
  const ctx: Ctx = {
    decode: [[]], // maps leaf to 0
    cache: {},
  };
  const alloc = (x: Shallow_node) => ctx.decode.push(x) - 1;
  const apply = (aid: Id, bid: Id): Id => {
    const cache = (ctx.cache[aid] ?? (ctx.cache[aid] = {}));
    return cache[bid] ?? (cache[bid] = (() => {
      const a = ctx.decode[aid];
      switch (a.length) {
        case 0: return alloc([bid]);
        case 1: return alloc([a[0], bid]);
        case 2:
          const [xid, yid] = a;
          const x = ctx.decode[xid];
          debug.num_steps++;
          switch (x.length) {
            case 0: return yid;
            case 1: return apply(apply(x[0], bid), apply(yid, bid));
            case 2:
              const b = ctx.decode[bid];
              switch (b.length) {
                case 0: return x[0];
                case 1: return apply(x[1], b[0]);
                case 2: return apply(apply(a[1], b[0]), b[1]);
              }
          }
      }
    })())
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
      switch (x.length) {
        case 0: return on_leaf();
        case 1: return on_stem(x[0]);
        case 2: return on_fork(x[0], x[1]);
      }
    }
  };
  return evaluator;
}

const debug = { num_steps: 0 };
export { debug };
export default make_evaluator;
