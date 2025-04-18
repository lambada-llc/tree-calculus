import { assert_equal, Evaluator } from "../common.mjs";
import { equal_ternary, id_ternary, succ_dag } from "../example-programs.mjs";
import formatter_dag from "./dag.mjs";
import formatter_ternary from "./ternary.mjs";
import formatter_readable from "./readable.mjs";
import { Formatter } from "./formatter.mjs";

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
}
