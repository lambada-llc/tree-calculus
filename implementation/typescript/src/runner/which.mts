// Which evaluator this process reduces with, as TREE_CALCULUS_RUNNER chooses
// it: unset, Node's own; `1`, the native runner's lazy evaluator; `eager`, its
// eager one.

const choice = () => process.env.TREE_CALCULUS_RUNNER ?? '';

/**
 * Whether to build the runner's eager evaluator, which is faster but only
 * terminates on a module whose every binding has a normal form. Opting in is a
 * claim about the module, so it is the caller's to make: TREE_CALCULUS_RUNNER=eager.
 */
export const eager = (): boolean => choice() === 'eager';

export const native = (): boolean => eager() || choice() === '1';

/**
 * The evaluator by name, and the version of what its cache entries claim.
 *
 * Every evaluator computes the same normal form, but an entry also says that
 * this evaluator *reached* it, and a hit skips the evaluation that would show
 * otherwise. An answer the lazy evaluator found for a term the eager one never
 * finishes must not pass an eager build, so each keeps its own. Bump a version
 * when what its entries claim changes.
 */
export const evaluator_name = (): string =>
  eager() ? 'eager-fastest-v1' : native() ? 'lazy-graph-nil-mmap-32-v1' : 'lazy-stacks-v1';
