import { Evaluator, raise } from "../common";
import { Formatter } from "./formatter";

// Format:
// Leaf = 0
// Stem u = 1u
// Fork u v = 2uv

export function to<TTree>(e: Evaluator<TTree>, x: TTree): string {
  const res: string[] = [];
  const triage = e.triage<void>(
    () => res.push('0'),
    u => (res.push('1'), triage(u)),
    (u, v) => (res.push('2'), triage(u), triage(v)));
  triage(x);
  return res.join('');
}

export function of<TTree>(e: Evaluator<TTree>, s: string): TTree {
  const stack = s.split('').reverse();
  const f = (): TTree => {
    const c = stack.pop();
    if (c === undefined) raise('unexpected end of ternary encoding');
    switch (c) {
      case '0': return e.leaf;
      case '1': return e.stem(f());
      case '2': return e.fork(f(), f());
      default: return raise(`unexpected character in ternary encoding: ${c}`);
    }
  };
  return f();
}

const formatter: Formatter = { to, of };
export default formatter;
