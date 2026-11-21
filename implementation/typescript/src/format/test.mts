import { assert_equal, Evaluator } from "../common.mjs";
import { equal_ternary, id_ternary, succ_dag } from "../example-programs.mjs";
import formatter_dag from "./dag.mjs";
import formatter_ternary from "./ternary.mjs";
import formatter_readable from "./readable.mjs";
import formatter_minbin from "./minbin.mjs";
import { Formatter } from "./formatter.mjs";
import * as lamz from "./lamz.mjs";
import { marshal } from "../common.mjs";

// Evaluator to use for this test -- any valid one works
import e from "../evaluator/eager-stacks.mjs";
type TTree = typeof e extends Evaluator<infer TTree> ? TTree : never;

function assert_equal_tree(expected: TTree, actual: TTree, test_case: string) {
  assert_equal(
    formatter_ternary.to(e, expected),
    formatter_ternary.to(e, actual), test_case);
}

// Serialization followed by deserialization preserves tree structure:
// of ∘ to = id
function assert_roundtrips_weak(formatter: Formatter, x: TTree) {
  const s = formatter.to(e, x);
  const x2 = formatter.of(e, s);
  assert_equal_tree(x, x2, 'formatter round-trips');
}

// Deserialization followed by serialization preserves string representation:
// to ∘ of = id
function assert_roundtrips_strong(formatter: Formatter, s: string) {
  const x = formatter.of(e, s);
  const s2 = formatter.to(e, x);
  assert_equal(s, s2, 'formatter round-trips');
}

export function test() {
  assert_roundtrips_strong(formatter_ternary, id_ternary);
  assert_roundtrips_strong(formatter_ternary, equal_ternary);
  assert_roundtrips_weak(formatter_dag, formatter_ternary.of(e, id_ternary));
  assert_roundtrips_weak(formatter_dag, formatter_ternary.of(e, equal_ternary));
  assert_roundtrips_weak(formatter_dag, formatter_dag.of(e, succ_dag));
  assert_roundtrips_strong(formatter_readable, '△');
  assert_roundtrips_strong(formatter_readable, '△ (△ (△ △)) △');
  assert_roundtrips_strong(formatter_readable, '△ (△ △ △) △');
  assert_roundtrips_weak(formatter_readable, formatter_ternary.of(e, id_ternary));
  assert_roundtrips_weak(formatter_readable, formatter_ternary.of(e, equal_ternary));
  assert_roundtrips_weak(formatter_readable, formatter_dag.of(e, succ_dag));
  assert_roundtrips_strong(formatter_minbin, '1');              // △
  assert_roundtrips_strong(formatter_minbin, '001010111');      // identity: △ (△ (△ △)) △
  assert_roundtrips_strong(formatter_minbin, '00111');          // △ △ △
  assert_roundtrips_weak(formatter_minbin, formatter_ternary.of(e, id_ternary));
  assert_roundtrips_weak(formatter_minbin, formatter_ternary.of(e, equal_ternary));
  assert_roundtrips_weak(formatter_minbin, formatter_dag.of(e, succ_dag));
  test_lamz();
}

function test_lamz() {
  const m = marshal(e);
  const bits = (byte: number) => m.of_list([7, 6, 5, 4, 3, 2, 1, 0].map(i => m.of_bool(((byte >> i) & 1) === 1)));
  const A = (f: lamz.Term, x: lamz.Term): lamz.Term => ({ t: 'A', f, x });
  const L = (body: lamz.Term): lamz.Term => ({ t: 'L', body });
  const V = (i: number): lamz.Term => ({ t: 'V', i });
  const G = (j: number): lamz.Term => ({ t: 'G', j });
  const C: lamz.Term = { t: 'C' };
  const check = (module: lamz.Term[], tail: number[], expected: TTree, test_case: string) => {
    const bytes = lamz.encode(module, new Uint8Array(tail));
    const decoded = lamz.decode(bytes);
    assert_equal(JSON.stringify(module), JSON.stringify(decoded.module), `${test_case}: module round-trips`);
    assert_equal(tail.join(), [...decoded.tail].join(), `${test_case}: tail round-trips`);
    assert_equal_tree(expected, lamz.of(e, bytes), test_case);
    assert_equal_tree(expected, formatter_dag.of(e, lamz.to_dag(bytes)), `${test_case}, via the DAG`);
  };
  // The program is handed the tail's bits.
  check([L(V(0))], [0xa5], bits(0xa5), 'identity');
  // A later term uses an earlier one, under λs of its own: K = λa b. a.
  check([L(L(V(1))), L(A(A(G(0), V(0)), C))], [0x0f], bits(0x0f), 'reference');
  // A λ body does not run before the λ is applied: the second term would loop
  // if it did (ω ω, with ω = λx. x x), but the first discards it.
  const omega = L(A(V(0), V(0)));
  check([L(L(V(1))), L(A(A(G(0), V(0)), L(A(omega, omega))))], [], m.of_list([]), 'call by value');
}
