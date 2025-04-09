import { Evaluator } from "../common.mjs";

type F = ((f: () => F) => () => F) & ({ x?: () => F, y?: () => F });

const share = (lazy: () => F): () => F => { let cache: F | null = null; return () => cache ?? (cache = lazy()); };
const bind1 = (f: F, x: () => F): F => (f.x = x, f);
const bind2 = (f: F, x: () => F, y: () => F): F => (f.x = x, f.y = y, f);
const leaf: F = dx =>
  () => bind1(y =>
    () => bind2((dz: () => F) => {
      const x = dx();
      if (x.y) {
        const xy = x.y;
        const z = dz();
        if (z.y) return share(() => y()(z.x!)()(z.y!)());
        if (z.x) return share(() => xy()(z.x!)());
        return x.x!;
      }
      if (x.x) return share(() => x.x!()(dz)()(y()(dz))());
      return y;
    }, dx, y), dx);

const evaluator: Evaluator<() => F> = {
  // construct
  leaf: () => leaf,
  stem: u => leaf(u),
  fork: (u, v) => leaf(u)()(v),
  // eval
  apply: (a, b) => a()(b),
  // destruct
  triage: (on_leaf, on_stem, on_fork) => dx => {
    const x = dx();
    if (x.y) return on_fork(x.x!, x.y);
    if (x.x) return on_stem(x.x);
    return on_leaf();
  }
};

export default evaluator;
