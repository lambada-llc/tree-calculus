import { Evaluator, marshal, measure, raise } from "./common";
import { bench_alloc_and_identity_ternary, bench_recursive_fib_ternary, equal_ternary, size_ternary, succ_dag } from "./example_programs";
import { of_dag, to_dag } from "./format/dag";
import { of_ternary, to_ternary } from "./format/ternary";
import array_mutable from "./evaluator/array-mutable";
import func_eager from "./evaluator/func-eager";
import { abs, app, marshal_term, node, Term_Lambda, variable } from "./lambda/term";
import { bracket_ski, star_ski, star_ski_eta, star_skibc_op_eta } from "./lambda/abs-elimination";

const evaluators: { [name: string]: Evaluator<any> } = {
  array_mutable,
  func_eager,
};

function test_evaluator<TTree>(name: string, e: Evaluator<TTree>) {
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
  // evaluation
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
  // performance
  const bench_recursive_fib = of_ternary(e, bench_recursive_fib_ternary);
  const fib20 = measure(() => m.to_nat(e.apply(bench_recursive_fib, m.of_nat(20n))));
  console.assert(10946n === fib20.result);
  console.debug("recursive fib 20:", fib20.elasped_ms + "ms");
  const bench_alloc_and_identity = of_ternary(e, bench_alloc_and_identity_ternary);
  const alloc_id = measure(() => m.to_string(e.apply(e.apply(bench_alloc_and_identity, m.of_nat(1000000n)), m.of_string("hello world"))));
  console.assert("hello world" === alloc_id.result);
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
    {
      // small [wait] program (applied to dummy program △, so subtract 1 for the size of [wait] itself)
      console.group('wait');
      const wait_node = wait(node);
      console.debug('bracket_ski', size(bracket_ski(wait_node)) - 1n);
      console.debug('star_ski', size(star_ski(wait_node)) - 1n);
      console.debug('star_ski_eta', size(star_ski_eta(wait_node)) - 1n);
      console.debug('star_skibc_eta', size(star_skibc_op_eta(wait_node)) - 1n);
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

      // quick sanity check
      for (const elim_to_check of [star_ski, star_ski_eta, star_skibc_op_eta]) {
        const size_to_test = term(elim_to_check(size_lambda));
        const chain_to_n = (x: TTree): bigint => e.triage(() => 0n, u => 1n + chain_to_n(u), (u, v) => raise('unexpexted'))(x);
        for (const test_term of [e.leaf, e.stem(e.leaf), e.fork(e.leaf, e.leaf), size_tree, size_to_test])
          console.assert(m.to_nat(e.apply(size_tree, test_term)) === chain_to_n(e.apply(size_to_test, test_term)), 'invalid size program');
      }

      // console.debug('bracket_ski', size(bracket_ski(size_lambda)));
      console.debug('star_ski', size(star_ski(size_lambda)));
      console.debug('star_ski_eta', size(star_ski_eta(size_lambda)));
      console.debug('star_skibc_eta', size(star_skibc_op_eta(size_lambda)));
      console.groupEnd();
    }
  }

  console.groupEnd();
}

test_abs_elimination(array_mutable);