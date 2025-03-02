import { assert_equal, Evaluator, marshal, raise } from "../common";
import { size_ternary } from "../example-programs";
import { of_ternary } from "../format/ternary";
import { abs, app, marshal_term, node, Term_Lambda, variable } from "./term";
import { bracket_ski, kiselyov_eta, kiselyov_kopt, kiselyov_plain, star_ski, star_ski_eta, star_skibc_op_eta } from "./strategies";

// Evaluator to use for this test -- any valid one works
import e from "../evaluator/eager-stacks";
type TTree = typeof e extends Evaluator<infer TTree> ? TTree : never;

const m = marshal(e);
const term = marshal_term(e);
const size_tree = of_ternary(e, size_ternary);
const size = (x: Term_Lambda): bigint => m.to_nat(e.apply(size_tree, term(x)));

const decent_eliminators = {
  star_ski,
  star_ski_eta,
  star_skibc_op_eta,
  kiselyov_plain,
  kiselyov_kopt,
  kiselyov_eta,
};
const eliminators = {
  bracket_ski, // OOMs on larger programs
  ...decent_eliminators
}

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

function test_wait() {
  // small [wait] program (applied to dummy program △, so subtract 1 for the size of [wait] itself)
  console.group('wait');
  const wait_node = wait(node);
  const evaluate = (elim: (term: Term_Lambda) => Term_Lambda) => size(elim(wait_node)) - 1n;
  for (const [elim_name, elim] of Object.entries(eliminators))
    console.debug(elim_name, evaluate(elim));
  // records
  assert_equal(16n, evaluate(star_skibc_op_eta), 'record');
  assert_equal(16n, evaluate(kiselyov_eta), 'record');
  console.groupEnd();
}

function test_fix() {
  // small fixed-point program (applied to dummy program △, so subtract 1 for the size of [wait] itself)
  console.group('fix');
  const fix_node = fix(node);
  const evaluate = (elim: (term: Term_Lambda) => Term_Lambda) => size(elim(fix_node)) - 1n;
  for (const [elim_name, elim] of Object.entries(eliminators))
    console.debug(elim_name, evaluate(elim));
  // records
  assert_equal(44n, evaluate(star_skibc_op_eta), 'record');
  assert_equal(44n, evaluate(kiselyov_eta), 'record');
  console.groupEnd();
}

function test_size() {
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
  const evaluate = (elim: (term: Term_Lambda) => Term_Lambda) => size(elim(size_lambda));

  for (const [elim_name, elim] of Object.entries(decent_eliminators)) {
    // sanity check behavior
    const size_to_test = term(elim(size_lambda));
    const chain_to_n = (x: TTree): bigint => e.triage<bigint>(() => 0n, u => 1n + chain_to_n(u), (u, v) => raise('unexpected'))(x);
    for (const test_term of [e.leaf, e.stem(e.leaf), e.fork(e.leaf, e.leaf), size_tree, size_to_test])
      assert_equal(
        m.to_nat(e.apply(size_tree, test_term)),
        chain_to_n(e.apply(size_to_test, test_term)),
        'size');

    console.debug(elim_name, evaluate(elim));
  }
  // records
  assert_equal(168n, evaluate(star_skibc_op_eta), 'record');
  assert_equal(168n, evaluate(kiselyov_eta), 'record');
  console.groupEnd();
}

function test_bf() {
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
  const evaluate = (elim: (term: Term_Lambda) => Term_Lambda) => size(elim(bf));

  for (const [elim_name, elim] of Object.entries(decent_eliminators)) {
    // sanity check behavior
    const bf_to_test = term(elim(bf));
    for (const test_term of [e.leaf, e.stem(e.leaf), e.fork(e.leaf, e.leaf), size_tree, bf_to_test])
      assert_equal(
        m.to_nat(e.apply(size_tree, test_term)),
        m.to_nat(e.apply(e.apply(bf_to_test, size_tree), test_term)),
        'bf');

    console.debug(elim_name, evaluate(elim));
  }
  // records
  assert_equal(349n, evaluate(kiselyov_eta), 'record');
  console.groupEnd();
}


export function test() {
  console.group('Abstraction elimination');
  test_wait();
  test_fix();
  test_size();
  test_bf();
  console.groupEnd();
}