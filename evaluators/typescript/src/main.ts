import { Evaluator, marshal } from "./common";
import array_mutable from "./strategies/array-mutable";
import func_eager from "./strategies/func-eager";

const evaluators: { [name: string]: Evaluator<any> } = {
  array_mutable,
  func_eager,
};

function test<TTree>(name: string, e: Evaluator<TTree>) {
  console.group(name);
  const m = marshal(e);

  const not = e.fork(e.fork(e.stem(e.leaf), e.fork(e.leaf, e.leaf)), e.leaf);

  console.debug('not false =', m.to_bool(e.apply(not, m.of_bool(false))));
  console.debug('not true =', m.to_bool(e.apply(not, m.of_bool(true))));
  console.groupEnd();
}

for (const [name, e] of Object.entries(evaluators))
  test(name, e);
