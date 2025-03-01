import { Evaluator, marshal, measure, raise } from "./common";
import { bench_alloc_and_identity_ternary, bench_linear_fib_ternary, bench_recursive_fib_ternary, equal_ternary, size_ternary, succ_dag } from "./example_programs";
import { of_dag, to_dag } from "./format/dag";
import { of_ternary, to_ternary } from "./format/ternary";
import eager_func from "./evaluator/eager-func";
import eager_node_app from "./evaluator/eager-node-app";
import eager_stacks from "./evaluator/eager-stacks";
import eager_value_adt from "./evaluator/eager-value-adt";
import eager_value_mem from "./evaluator/eager-value-mem";
import lazy_value_adt from "./evaluator/lazy-value-adt";
import { abs, app, marshal_term, node, Term_Lambda, variable } from "./lambda/term";
import { bracket_ski, kiselyov_eta, kiselyov_kopt, kiselyov_plain, star_ski, star_ski_eta, star_skibc_op_eta } from "./lambda/abs-elimination";

const evaluators: { [name: string]: Evaluator<any> } = {
  eager_func,
  eager_node_app,
  eager_stacks,
  eager_value_adt,
  lazy_value_adt, // prone to stack overflow
  eager_value_mem: eager_value_mem(), // does not free memory
};

const assertEqual = <T>(expected: T, actual: T, testCase: string) =>
  console.assert(expected === actual, `expected: ${expected}, actual: ${actual}, test: ${testCase}`);

function test_evaluator<TTree>(name: string, e: Evaluator<TTree>) {
  console.group(name);
  const m = marshal(e);
  const tt = m.of_bool(true);
  const ff = m.of_bool(false);
  const not = e.fork(e.fork(e.stem(e.leaf), e.fork(e.leaf, e.leaf)), e.leaf);

  // ternary
  const equal = of_ternary(e, equal_ternary);
  assertEqual(equal_ternary, to_ternary(e, equal), 'ternary formatter round-trips');
  {
    // basic reduction rule check
    const ruleCheck = (rule: string, expected: string, a: string, b: string) =>
      assertEqual(expected, to_ternary(e, e.apply(of_ternary(e, a), of_ternary(e, b))), "rule " + rule);
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
  // dag
  const succ = of_dag(e, succ_dag);
  const succ_roundtrip = of_dag(e, to_dag(e, of_dag(e, succ_dag)));
  assertEqual(to_ternary(e, succ), to_ternary(e, succ_roundtrip), 'dag formatter round-trips');
  // evaluation
  assertEqual(true, m.to_bool(e.apply(not, ff)), 'not');
  assertEqual(false, m.to_bool(e.apply(not, tt)), 'not');
  for (const a of [not, tt, ff])
    for (const b of [not, tt, ff])
      assertEqual(
        (a === b),
        m.to_bool(e.apply(e.apply(e.apply(e.apply(equal, a), b), tt), ff)),
        'equal');
  for (const i of [0n, 1n, 2n, 3n, 7n, 8n, 65535n, 65535n + 8n])
    assertEqual(
      i + 1n,
      m.to_nat(e.apply(succ, m.of_nat(i))),
      'succ');
  // performance
  const bench_linear_fib = of_ternary(e, bench_linear_fib_ternary);
  const fib100 = measure(() => m.to_nat(e.apply(bench_linear_fib, m.of_nat(100n))));
  assertEqual(573147844013817084101n, fib100.result, "fib 100");
  console.debug("linear fib 100:", fib100.elasped_ms + "ms");
  if (e !== lazy_value_adt as any) { // stack overflow
    const bench_recursive_fib = of_ternary(e, bench_recursive_fib_ternary);
    const fib23 = measure(() => m.to_nat(e.apply(bench_recursive_fib, m.of_nat(23n))));
    assertEqual(46368n, fib23.result, "fib 23");
    console.debug("recursive fib 23:", fib23.elasped_ms + "ms");
  }
  const bench_alloc_and_identity = of_ternary(e, bench_alloc_and_identity_ternary);
  const alloc_id = measure(() => m.to_string(e.apply(e.apply(bench_alloc_and_identity, m.of_nat(1000000n)), m.of_string("hello world"))));
  assertEqual("hello world", alloc_id.result, "identity with needless allocation");
  console.debug("alloc and identity:", alloc_id.elasped_ms + "ms");
  console.groupEnd();
}

for (const [name, e] of Object.entries(evaluators))
  test_evaluator(name, e);

function test_abs_elimination<TTree>(e: Evaluator<TTree>) {
  console.group('Abstraction elimination');
  const m = marshal(e);
  const term = marshal_term(e);
  const size_tree = of_ternary(e, size_ternary);
  const size = (x: Term_Lambda): bigint => m.to_nat(e.apply(size_tree, term(x)));

  {
    const triage = (u: Term_Lambda, v: Term_Lambda, w: Term_Lambda) => app(node, app(node, u, v), w);
    const s1 = (u: Term_Lambda) => app(node, app(node, u));
    const s = star_ski_eta(abs('u', s1(variable('u'))));
    const triage_op = star_ski_eta(abs('u', abs('v', abs('w', triage(variable('u'), variable('v'), variable('w'))))));
    const k = app(node, node);
    const k1 = (u: Term_Lambda) => app(k, u);
    const i = app(s1(k), node);
    const compose = (f: Term_Lambda, g: Term_Lambda) => abs('compose_x', app(f, app(g, variable('compose_x'))));
    // const compose = (f: Term_Lambda, g: Term_Lambda) => app(node, app(node, app(k, f)), g);
    const self_apply = abs('x', app(variable('x'), variable('x')));
    const self_apply_k = abs('x', app(variable('x'), k1(variable('x'))));
    const wait = (a: Term_Lambda) => abs('b', abs('c', app(s1(a), k1(variable('c')), variable('b'))));
    const wait1 = (a: Term_Lambda) => s1(app(s1(k1(s1(a))), k));
    const fix = (functional: Term_Lambda) => app(wait(self_apply_k), abs('x', app(functional, app(wait1(self_apply_k), variable('x')))));
    const decent_eliminators = { // i.e. all but bracket, which would OOM tests further down
      star_ski,
      star_ski_eta,
      star_skibc_op_eta,
      kiselyov_plain,
      kiselyov_kopt,
      kiselyov_eta,
    };
    {
      // small [wait] program (applied to dummy program △, so subtract 1 for the size of [wait] itself)
      console.group('wait');
      const wait_node = wait(node);
      console.debug('bracket_ski', size(bracket_ski(wait_node)) - 1n);
      for (const [elim_name, elim] of Object.entries(decent_eliminators))
        console.debug(elim_name, size(elim(wait_node)) - 1n);
      console.groupEnd();
    }
    {
      // small [size] program
      console.group('size');
      const zero = node;
      const succ = node;
      // number' := number -> number
      // _triage :: tree -> (tree -> number') -> number'
      const _triage = triage(
        abs('self', abs('n', variable('n'))),
        abs('u', abs('self', app(variable('self'), variable('u')))),
        abs('u', abs('v', abs('self', compose(app(variable('self'), variable('u')), app(variable('self'), variable('v')))))),
      );
      // _functional:: (tree -> number') -> (tree -> number')
      const _functional = abs('self', abs('x', compose(succ, app(_triage, variable('x'), variable('self')))));
      // _size:: tree -> number'
      const _size = fix(_functional);
      // size:: tree -> number
      const size_lambda = abs('x', app(_size, variable('x'), zero));

      for (const [elim_name, elim] of Object.entries(decent_eliminators)) {
        // sanity check behavior
        const size_to_test = term(elim(size_lambda));
        const chain_to_n = (x: TTree): bigint => e.triage(() => 0n, u => 1n + chain_to_n(u), (u, v) => raise('unexpected'))(x);
        for (const test_term of [e.leaf, e.stem(e.leaf), e.fork(e.leaf, e.leaf), size_tree, size_to_test])
          assertEqual(
            m.to_nat(e.apply(size_tree, test_term)),
            chain_to_n(e.apply(size_to_test, test_term)),
            'size');

        console.debug(elim_name, size(elim(size_lambda)));
      }
      console.groupEnd();
    }
    {
      // small [bf] branch first self-evaluator
      console.group('bf');
      const eager_s = triage(
        abs('f', app(variable('f'), node)),
        abs('u', abs('f', app(variable('f'), app(node, variable('u'))))),
        abs('u', abs('v', abs('f', app(variable('f'), app(node, variable('u'), variable('v')))))),
      );
      const eager = abs('f', abs('x', app(eager_s, variable('x'), variable('f'))));
      // const bffs = abs('x', abs('e', abs('y', abs('z', app(
      //   eager_s,
      //   app(variable('e'), variable('y'), variable('z')),
      //   app(variable('e'), app(variable('e'), variable('x'), variable('z')))
      // )))));
      const bffs = abs('x', abs('e', app(
        s1(abs('y', app(s, abs('z', app(
          eager_s,
          app(variable('e'), variable('y'), variable('z')),
        ))))),
        abs('y', abs('z', app(variable('e'), app(variable('e'), variable('x'), variable('z'))))))));
      const bfff = abs('w', abs('x', abs('e', abs('y', triage(
        variable('w'),
        app(variable('e'), variable('x')),
        abs('z', app(variable('e'), app(variable('e'), variable('y'), variable('z'))))
      )))));
      const bff = abs('e', abs('x', app(triage(
        abs('e', k),
        bffs,
        bfff
      ), variable('x'), variable('e'))));
      const bf = fix(abs('e', triage(
        node,
        node,
        app(bff, variable('e'))
      )));

      for (const [elim_name, elim] of Object.entries(decent_eliminators)) {
        // sanity check behavior
        const bf_to_test = term(elim(bf));
        for (const test_term of [e.leaf, e.stem(e.leaf), e.fork(e.leaf, e.leaf), size_tree, bf_to_test])
          assertEqual(
            m.to_nat(e.apply(size_tree, test_term)),
            m.to_nat(e.apply(e.apply(bf_to_test, size_tree), test_term)),
            'bf');

        console.debug(elim_name, size(elim(bf)));
      }
      console.groupEnd();
    }
  }

  console.groupEnd();
}

test_abs_elimination(eager_stacks);