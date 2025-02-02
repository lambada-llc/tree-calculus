import { Evaluator } from "../common";

type F = ((f: F) => F) & ({ x?: F, y?: F });

const bind1 = (f: F, x: F): F => (f.x = x, f);
const bind2 = (f: F, x: F, y: F): F => (f.x = x, f.y = y, f);
const leaf: F = x =>
  bind1(y =>
    bind2((z: F) => {
      if (x.y) {
        if (z.y) return y(z.x!)(z.y);
        if (z.x) return x.y(z.x);
        return x.x!;
      }
      if (x.x) return x.x(z)(y(z));
      return y;
    }, x, y), x);

const evaluator: Evaluator<F> = {
  // construct
  leaf,
  stem: u => leaf(u),
  fork: (u, v) => leaf(u)(v),
  // eval
  apply: (a, b) => a(b),
  // destruct
  triage: (on_leaf, on_stem, on_fork) => x => {
    if (x.y) return on_fork(x.x!, x.y);
    if (x.x) return on_stem(x.x);
    return on_leaf;
  }
};

export default evaluator;
