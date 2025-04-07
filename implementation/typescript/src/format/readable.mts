import { Evaluator, raise } from "../common.mjs";
import { Formatter } from "./formatter.mjs";

export function to<TTree>(e: Evaluator<TTree>, x: TTree): string {
  const triage: (x: TTree) => string = e.triage<string>(
    () => '△',
    u => `(△ ${triage(u)})`,
    (u, v) => `(△ ${triage(u)} ${triage(v)})`);
  return e.triage<string>(
    () => '△',
    u => `△ ${triage(u)}`,
    (u, v) => `△ ${triage(u)} ${triage(v)}`)(x);
}

export function of<TTree>(e: Evaluator<TTree>, s: string): TTree {
  const id = e.fork(e.stem(e.stem(e.leaf)), e.leaf);
  const stack: TTree[] = [id];
  const apply = (x: TTree) => stack[stack.length - 1] = e.apply(stack[stack.length - 1] || raise('unmatched parentheses'), x);
  for (const c of s) {
    switch (c) {
      case '△': apply(e.leaf); break;
      case '(': stack.push(id); break;
      case ')': apply(stack.pop() || raise('unmatched parentheses')); break;
      case ' ': break;
      default: raise(`unexpected character: ${c}`);
    }
  }
  const res = stack.pop();
  if (res === undefined || stack.length > 0)
    return raise('unmatched parentheses');
  return res;
}

const formatter: Formatter = { to, of };
export default formatter;
