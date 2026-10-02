// Running a tree as a text transformation.
//
// A great many useful trees are functions from text to text — compilers,
// formatters, translators. Using one means marshalling the input into a tree,
// applying, and marshalling back:
//
//   const compile = transformer(e, compiler_dag);
//   compile('\\x x');
//
// Because a tree is a pure function, the same input always gives the same
// output, which makes the result safe to keep on disk forever. With
// TREE_CALCULUS_CACHE set it is kept there, beside every other reduction cache
// (see cache.mts), keyed on the transformation and its input together, so a
// rebuild only pays for the parts that actually changed — and changing the
// program correctly invalidates everything.

import { createHash } from "crypto";
import { Evaluator, marshal } from "../common.mjs";
import formatter_dag from "../format/dag.mjs";
import { store, text_key, TRANSFORM_STORE } from "./cache.mjs";

/**
 * `run`, with its results kept in the transform store, keyed by the program
 * and the input.
 *
 * Separate from `transformer` because who *runs* the tree is not fixed — the
 * native runner in ../runner/native.mts transforms the same text with the same
 * caching, and the cache should not know or care which of them produced a
 * result.
 */
export function memoize(
  run: (input: string) => string,
  program: string,
): (input: string) => string {
  const kept = store(TRANSFORM_STORE);
  if (!kept) return run;

  const program_key = text_key(program).toString('hex');
  return (input: string): string => {
    const key = createHash('sha256').update(`${program_key}\n${input}`).digest();
    const hit = kept.get(key);
    if (hit) return hit.toString('utf8');
    const output = run(input);
    kept.put(key, output);
    return output;
  };
}

/**
 * A text-to-text function backed by the tree that `program` (DAG text) denotes.
 *
 * The program is only parsed and evaluated once something is actually
 * transformed, so a fully cached run never pays to build it.
 */
export function transformer<TTree>(
  e: Evaluator<TTree>,
  program: string,
): (input: string) => string {
  const m = marshal(e);
  let tree: TTree | null = null;
  return memoize((input: string): string => {
    tree ??= formatter_dag.of(e, program);
    return m.to_string(e.apply(tree, m.of_string(input)));
  }, program);
}
