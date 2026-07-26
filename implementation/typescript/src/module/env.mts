// Evaluating a DAG module into an environment.
//
// The `dag` formatter reads a DAG that ends in a value and gives you that one
// tree. A module names many trees and ends in none, so what it evaluates to is
// not a tree but a lookup: give it a symbol, get the tree bound to it.

import { Evaluator } from "../common.mjs";
import { LEAF } from "./module.mjs";

export interface EnvOptions {
  /** Prefixed to unbound-symbol errors raised while reading, e.g. a file path. */
  origin?: string;
}

/**
 * Evaluate every binding in `text` and return a lookup over the results.
 *
 * Bindings are evaluated as they are read, so a symbol must be defined above any
 * line that uses it — which is exactly what `link` arranges for.
 */
export function environment<TTree>(
  e: Evaluator<TTree>,
  text: string,
  options: EnvOptions = {},
): (symbol: string) => TTree {
  const { origin = '' } = options;
  const env: { [symbol: string]: TTree } = { [LEAF]: e.leaf };

  // Where we are while reading, so an unbound symbol points at the line that
  // wanted it. Empty once reading is done and lookups come from the caller.
  let context = '';
  const get = (symbol: string): TTree => {
    if (symbol in env) return env[symbol];
    throw new Error(`${context}unbound symbol: ${symbol}`);
  };

  let line_number = 0;
  for (const line of text.split(/\r?\n/)) {
    line_number++;
    context = `${origin ? `${origin}:` : ''}${line_number}: `;
    const words = line.split(' ');
    if (words.length === 3) env[words[0]] = e.apply(get(words[1]), get(words[2]));
    else if (words.length === 2) env[words[0]] = get(words[1]);
  }
  context = '';

  return get;
}
