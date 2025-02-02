import { Evaluator, raise } from "../common";

export function to_ternary<TTree>(e: Evaluator<TTree>, x: TTree): string {
  const res: string[] = [];
  const triage = e.triage<void>(
    () => res.push('0'),
    u => (res.push('1'), triage(u)),
    (u, v) => (res.push('2'), triage(u), triage(v)));
  triage(x);
  return res.join('');
}
export function of_ternary<TTree>(e: Evaluator<TTree>, s: string): TTree {
  const stack = s.split('').reverse();
  const f = (): TTree => {
    switch (stack.pop()) {
      case '0': return e.leaf;
      case '1': return e.stem(f());
      case '2': return e.fork(f(), f());
      default: return raise('unexpected character in ternary encoding');
    }
  };
  return f();
}
