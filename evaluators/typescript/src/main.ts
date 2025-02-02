import { marshal } from "./common";
import array_mutable from "./strategies/array-mutable";

const e = array_mutable;
const m = marshal(e);


const not = e.fork(e.fork(e.stem(e.leaf), e.fork(e.leaf, e.leaf)), e.leaf);

console.debug('not false =', m.to_bool(e.apply(not, m.of_bool(false))));
console.debug('not true =', m.to_bool(e.apply(not, m.of_bool(true))));
