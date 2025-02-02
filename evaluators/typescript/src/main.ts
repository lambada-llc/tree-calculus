import { Evaluator, marshal } from "./common";
import { of_ternary, to_ternary } from "./format/ternary";
import array_mutable from "./strategies/array-mutable";
import func_eager from "./strategies/func-eager";

// from "Typed Program Analysis without Encodings" (Jay, PEPM 2025)
const equal_ternary =
  `212121202120112110102121200212002120112002120
   112002121200212002120102120021200212120021200
   212010211010212010211010202120102110102020211
   010202120112110102121200212002120112002120112
   002121200212002120102120021200212120021200212
   010211010212010211010202120102110102020211010
   202120112220221020202110102020202110102121200
   212002120112002120112012021101021201121101021
   201021101020202020202110102120112011201220211
   010202021101021212002120021201120021201120021
   201120112002120112011200212011201120112002120
   112011201120021212002120021201021200212002120
   112002120112002120112011200212011201120021201
   120112010212120021200212011200212011200212011
   201021201121101021201021101020202110102021201
   120102121200212002120112002120112002120112010
   212011211010212010211010202021101020202020202
   021101010211010`.split(/\s/).join('');

const evaluators: { [name: string]: Evaluator<any> } = {
  array_mutable,
  func_eager,
};

function test<TTree>(name: string, e: Evaluator<TTree>) {
  console.group(name);
  const m = marshal(e);

  const tt = m.of_bool(true);
  const ff = m.of_bool(false);
  const not = e.fork(e.fork(e.stem(e.leaf), e.fork(e.leaf, e.leaf)), e.leaf);
  const equal = of_ternary(e, equal_ternary);
  console.assert(equal_ternary === to_ternary(e, equal), 'ternary formatter does not round-trip');
  console.assert(true === m.to_bool(e.apply(not, ff)), 'true != not false');
  console.assert(false === m.to_bool(e.apply(not, tt)), 'false != not true');
  for (const a of [not, tt, ff])
    for (const b of [not, tt, ff])
      console.assert(
        (a === b) === m.to_bool(e.apply(e.apply(e.apply(e.apply(equal, a), b), tt), ff)),
        'unexpected equality result');
  console.groupEnd();
}

for (const [name, e] of Object.entries(evaluators))
  test(name, e);

