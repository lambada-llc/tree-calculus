import { assert_equal, Evaluator } from "../common.mjs";
import { equal_ternary, id_ternary, succ_dag } from "../example-programs.mjs";
import formatter_dag from "./dag.mjs";
import formatter_ternary from "./ternary.mjs";
import formatter_readable from "./readable.mjs";
import formatter_minbin from "./minbin.mjs";
import { Formatter } from "./formatter.mjs";
import * as dagz from "./dagz.mjs";
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
  test_dagz();
}

function test_dagz() {
  const m = marshal(e);
  const check = (dag: dagz.Dag, expected: TTree, test_case: string) => {
    const bytes = dagz.encode(dag);
    assert_equal(dagz.to_dag(bytes), dagz.to_dag(dagz.encode(dagz.decode(bytes))), `${test_case}: DAG round-trips`);
    assert_equal_tree(expected, dagz.of(e, bytes), test_case);
    assert_equal_tree(expected, formatter_dag.of(e, dagz.to_dag(bytes)), `${test_case}, via the DAG`);
  };
  // I = △ (△ K) K with K = △ △, written once and used twice.
  const I: [number, number][] = [[0, 0], [0, 0], [0, 1], [0, 2], [3, 1]];
  const identity = formatter_ternary.of(e, '211010');
  check({ nodes: I, root: 4 }, identity, 'identity');
  // A node may be an application that still reduces: I I is I.
  check({ nodes: [...I, [4, 4]], root: 5 }, identity, 'unreduced');
  // A list of lists, as data: rows of bits.
  const rows = [[1, 0, 1], [0, 1, 1]];
  const nodes: [number, number][] = [[0, 0], [0, 0]];
  const app = (l: number, r: number) => (nodes.push([l, r]), nodes.length - 1);
  const list = (xs: number[]) => xs.reduceRight((rest, x) => app(app(0, x), rest), 0);
  const root = list(rows.map(row => list(row.map(b => b ? 1 : 0))));
  check({ nodes, root }, m.of_list(rows.map(row => m.of_list(row.map(b => m.of_bool(b === 1))))), 'rows');
}

