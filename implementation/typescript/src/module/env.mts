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

export interface Environment<TTree> {
  /** The tree bound to `symbol`. */
  (symbol: string): TTree;
  /**
   * The value of `text`, a DAG ending in one, read with this module's bindings
   * in scope but not into them: what it defines shadows nothing and is gone
   * when the value comes back.
   *
   * This is how a module and the expressions asked of it stay separable. Put
   * them in the module and reading it evaluates them all; ask them one at a
   * time and each costs what it costs, when it is asked.
   */
  reduce(text: string): TTree;
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
): Environment<TTree> {
  const { origin = '' } = options;
  const env: { [symbol: string]: TTree } = { [LEAF]: e.leaf };

  // Where we are while reading, so an unbound symbol points at the line that
  // wanted it. Empty once reading is done and lookups come from the caller.
  let context = '';

  /**
   * Read a DAG into `scope`, resolving whatever it does not define against the
   * module, and return the value it ends on if it ends on one.
   */
  const read = (text: string, scope: { [symbol: string]: TTree }): TTree | undefined => {
    const get = (symbol: string): TTree => {
      if (symbol in scope) return scope[symbol];
      if (symbol in env) return env[symbol];
      throw new Error(`${context}unbound symbol: ${symbol}`);
    };

    let value: TTree | undefined;
    let line_number = 0;
    for (const line of text.split(/\r?\n/)) {
      line_number++;
      context = `${origin ? `${origin}:` : ''}${line_number}: `;
      const words = line.split(' ');
      if (words.length === 3) scope[words[0]] = e.apply(get(words[1]), get(words[2]));
      else if (words.length === 2) scope[words[0]] = get(words[1]);
      else if (words[0]) value = get(words[0]);
    }
    context = '';
    return value;
  };

  read(text, env);

  const get = (symbol: string): TTree => {
    if (symbol in env) return env[symbol];
    throw new Error(`unbound symbol: ${symbol}`);
  };
  get.reduce = (text: string): TTree => {
    const value = read(text, Object.create(null));
    if (value === undefined) throw new Error('dag representation was not terminated by a value');
    return value;
  };
  return get;
}
