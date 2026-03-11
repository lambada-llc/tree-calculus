import { Evaluator, raise } from "../common.mjs";
import { Formatter } from "./formatter.mjs";

// Format:
// Preorder encoding of the expression tree where application is an inner node and △ is a leaf.
// Application (inner node) = 0
// △ (leaf) = 1
//
// In terms of tree calculus values:
// Leaf = 1
// Stem u = 01u        (i.e. app(△, u))
// Fork u v = 001uv    (i.e. app(app(△, u), v))

export function to<TTree>(e: Evaluator<TTree>, x: TTree): string {
  const res: string[] = [];
  const triage = e.triage<void>(
    () => res.push('1'),
    u => (res.push('0'), res.push('1'), triage(u)),
    (u, v) => (res.push('0'), res.push('0'), res.push('1'), triage(u), triage(v)));
  triage(x);
  return res.join('');
}

export function of<TTree>(e: Evaluator<TTree>, s: string): TTree {
  const stack = s.split('').reverse();
  const f = (): TTree => {
    const c = stack.pop();
    if (c === undefined) raise('unexpected end of minimalist binary encoding');
    switch (c) {
      case '1': return e.leaf;
      case '0': {
        const func = f();
        const arg = f();
        return e.apply(func, arg);
      }
      default: return raise(`unexpected character in minimalist binary encoding: ${c}`);
    }
  };
  const result = f();
  if (stack.length > 0) raise('trailing characters in minimalist binary encoding');
  return result;
}

const formatter: Formatter = { to, of };
export default formatter;
