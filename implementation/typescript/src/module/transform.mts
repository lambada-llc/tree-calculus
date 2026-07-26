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
// output, which makes the result safe to keep on disk forever. `cache_dir` does
// that, keyed on the transformation and its input together, so a rebuild only
// pays for the parts that actually changed — and changing the program correctly
// invalidates everything.

import { createHash } from "crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "fs";
import { resolve } from "path";
import { Evaluator, marshal } from "../common.mjs";
import formatter_dag from "../format/dag.mjs";

export interface TransformerOptions {
  /** Directory to memoize results in. Omit for no caching. */
  cache_dir?: string;
}

const sha256 = (s: string) => createHash('sha256').update(s).digest('hex');

/**
 * A text-to-text function backed by the tree that `program` (DAG text) denotes.
 *
 * The program is only parsed and evaluated once something is actually
 * transformed, so a fully cached run never pays to build it.
 */
export function transformer<TTree>(
  e: Evaluator<TTree>,
  program: string,
  options: TransformerOptions = {},
): (input: string) => string {
  const { cache_dir } = options;
  const m = marshal(e);

  let tree: TTree | null = null;
  const run = (input: string): string => {
    tree ??= formatter_dag.of(e, program);
    return m.to_string(e.apply(tree, m.of_string(input)));
  };

  if (cache_dir === undefined) return run;

  mkdirSync(cache_dir, { recursive: true });
  const program_hash = sha256(program);
  return (input: string): string => {
    const path = resolve(cache_dir, sha256(`${program_hash}\n${input}`));
    if (!existsSync(path)) writeFileSync(path, run(input));
    return readFileSync(path, 'utf8');
  };
}
