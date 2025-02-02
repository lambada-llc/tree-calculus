import { Evaluator, marshal } from "./common";
import { equal_ternary, succ_dag } from "./example_programs";
import { of_dag, to_dag } from "./format/dag";
import { of_ternary, to_ternary } from "./format/ternary";
import array_mutable from "./strategies/array-mutable";
import func_eager from "./strategies/func-eager";

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
  // ternary
  const equal = of_ternary(e, equal_ternary);
  console.assert(equal_ternary === to_ternary(e, equal), 'ternary formatter does not round-trip');
  // dag
  const succ = of_dag(e, succ_dag);
  const succ_roundtrip = of_dag(e, to_dag(e, of_dag(e, succ_dag)));
  console.assert(to_ternary(e, succ) === to_ternary(e, succ_roundtrip), 'dag formatter does not round-trip');

  console.assert(true === m.to_bool(e.apply(not, ff)), 'true != not false');
  console.assert(false === m.to_bool(e.apply(not, tt)), 'false != not true');
  for (const a of [not, tt, ff])
    for (const b of [not, tt, ff])
      console.assert(
        (a === b) === m.to_bool(e.apply(e.apply(e.apply(e.apply(equal, a), b), tt), ff)),
        'unexpected equality result');
  for (const i of [0n, 1n, 2n, 3n, 7n, 8n, 65535n, 65535n + 8n])
    console.assert(
      i + 1n === m.to_nat(e.apply(succ, m.of_nat(i))),
      'succ behaved unexpectedly');
  console.groupEnd();
}

for (const [name, e] of Object.entries(evaluators))
  test(name, e);

