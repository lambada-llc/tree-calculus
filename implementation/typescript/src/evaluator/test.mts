import { assert_equal, Evaluator, marshal, measure, raise } from "../common.mjs";
import { bench_alloc_and_identity_ternary, bench_linear_fib_ternary, bench_recursive_fib_ternary, equal_ternary, size_ternary, succ_dag } from "../example-programs.mjs";
import formatter_dag from "../format/dag.mjs";
import formatter_ternary from "../format/ternary.mjs";
import eager_func from "./eager-func.mjs";
import eager_node_app from "./eager-node-app.mjs";
import eager_stacks from "./eager-stacks.mjs";
import eager_value_adt from "./eager-value-adt.mjs";
import eager_value_memory from "./eager-value-memory.mjs";
import lazy_func from "./lazy-func.mjs";
import lazy_stacks from "./lazy-stacks.mjs";
import lazy_value_adt from "./lazy-value-adt.mjs";
import lazy_value_memory from "./lazy-value-memory.mjs";

function test_basic_reduction_rules<TTree>(e: Evaluator<TTree>) {
  // basic reduction rule check
  const ruleCheck = (rule: string, expected: string, a: string, b: string) =>
    assert_equal(expected, formatter_ternary.to(e, e.apply(formatter_ternary.of(e, a), formatter_ternary.of(e, b))), "rule " + rule);
  const tl = '0';
  const ts = '10';
  const tf = '200';
  const t = [tl, ts, tf]; // some simple trees
  for (const z of t)
    ruleCheck('0a', '1' + z, '0', z);
  for (const y of t)
    for (const z of t)
      ruleCheck('0b', '2' + y + z, '1' + y, z);
  for (const y of t)
    for (const z of t)
      ruleCheck('1', y, '20' + y, z);
  for (const z of t)
    ruleCheck('2', '2' + z + '1' + z, '2100', z); // x = 0, y = 0
  for (const yc of t)
    for (const z of t)
      ruleCheck('2', '2' + z + '2' + yc + z, '2101' + yc, z); // x = 0, y = 1+yz
  for (const y of t)
    for (const z of t)
      ruleCheck('2', z, '2110' + y, z); // x = 10
  for (const w of t)
    for (const x of t)
      for (const y of t)
        ruleCheck('3a', w, '22' + w + x + y, '0');
  for (const w of t)
    for (const y of t)
      for (const u of t)
        ruleCheck('3b', '1' + u, '22' + w + '0' + y, '1' + u); // x = 0
  for (const w of t)
    for (const y of t)
      for (const u of t)
        ruleCheck('3b', '20' + u, '22' + w + '10' + y, '1' + u); // x = 10
  for (const w of t)
    for (const x of t)
      for (const u of t)
        for (const v of t)
          ruleCheck('3c', '2' + u + v, '22' + w + x + '0', '2' + u + v); // y = 0
  for (const w of t)
    for (const x of t)
      for (const u of t)
        for (const v of t)
          ruleCheck('3c', u, '22' + w + x + '10', '2' + u + v); // y = 10
}

function benchmark<TTree>(e: Evaluator<TTree>) {
  const m = marshal(e);
  const bench_linear_fib = formatter_ternary.of(e, bench_linear_fib_ternary);
  const fib100 = measure(() => m.to_nat(e.apply(bench_linear_fib, m.of_nat(100n))));
  assert_equal(573147844013817084101n, fib100.result, "fib 100");
  console.debug("linear fib 100:", fib100.elasped_ms + "ms");
  if (e !== lazy_func as any && e !== lazy_value_adt as any) { // stack overflow
    const bench_recursive_fib = formatter_ternary.of(e, bench_recursive_fib_ternary);
    const fib23 = measure(() => m.to_nat(e.apply(bench_recursive_fib, m.of_nat(23n))));
    assert_equal(46368n, fib23.result, "fib 23");
    console.debug("recursive fib 23:", fib23.elasped_ms + "ms");
  }
  const bench_alloc_and_identity = formatter_ternary.of(e, bench_alloc_and_identity_ternary);
  const alloc_id = measure(() => m.to_string(e.apply(e.apply(bench_alloc_and_identity, m.of_nat(1000000n)), m.of_string("hello world"))));
  assert_equal("hello world", alloc_id.result, "identity with needless allocation");
  console.debug("alloc and identity:", alloc_id.elasped_ms + "ms");
  console.groupEnd();
}

function test_evaluator<TTree>(name: string, e: Evaluator<TTree>) {
  console.group(name);
  const m = marshal(e);
  const tt = m.of_bool(true);
  const ff = m.of_bool(false);
  const not = e.fork(e.fork(e.stem(e.leaf), e.fork(e.leaf, e.leaf)), e.leaf);
  const equal = formatter_ternary.of(e, equal_ternary);
  const succ = formatter_dag.of(e, succ_dag);

  // evaluation
  test_basic_reduction_rules(e);
  assert_equal(true, m.to_bool(e.apply(not, ff)), 'not');
  assert_equal(false, m.to_bool(e.apply(not, tt)), 'not');
  for (const a of [not, tt, ff])
    for (const b of [not, tt, ff])
      assert_equal(
        (a === b),
        m.to_bool(e.apply(e.apply(e.apply(e.apply(equal, a), b), tt), ff)),
        'equal');
  for (const i of [0n, 1n, 2n, 3n, 7n, 8n, 65535n, 65535n + 8n])
    assert_equal(
      i + 1n,
      m.to_nat(e.apply(succ, m.of_nat(i))),
      'succ');

  // benchmark
  benchmark(e);
}

const evaluators: { [name: string]: Evaluator<any> } = {
  eager_func,
  lazy_func, // prone to stack overflow
  eager_node_app,
  eager_stacks,
  lazy_stacks,
  eager_value_adt,
  lazy_value_adt, // prone to stack overflow
  eager_value_memory: eager_value_memory(), // does not free memory
  lazy_value_memory: lazy_value_memory(), // does not free memory
};

export function test() {
  for (const [name, e] of Object.entries(evaluators))
    test_evaluator(name, e);
}