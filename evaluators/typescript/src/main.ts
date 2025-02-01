import { leaf, stem, fork, apply, of_bool, to_bool } from "./implementations/array-mutable";

const not = fork(fork(stem(leaf), fork(leaf, leaf)), leaf);

console.debug('not false =', to_bool(apply(not, of_bool(false))));
console.debug('not true =', to_bool(apply(not, of_bool(true))));
