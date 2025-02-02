export interface Evaluator<TTree> {
  // construct
  leaf: TTree;
  stem: (u: TTree) => TTree;
  fork: (u: TTree, v: TTree) => TTree;
  // eval
  apply: (a: TTree, b: TTree) => TTree;
  // destruct
  triage: <T>(on_leaf: () => T, on_stem: (u: TTree) => T, on_fork: (u: TTree, v: TTree) => T) => (x: TTree) => T;
}

export const raise = (message: string) => { throw new Error(message); }

export interface Marshaller<TTree> {
  // false == △  and  true == △ △
  to_bool: (x: TTree) => boolean;
  of_bool: (x: boolean) => TTree;
  // nil == △  and  hd :: tl == △ hd tl
  to_list: (x: TTree) => TTree[];
  of_list: (x: TTree[]) => TTree;
  // nat = list of bools (LSB first)
  to_nat: (x: TTree) => bigint;
  of_nat: (x: bigint) => TTree;
  // str = list of nats (Unicode code points)
  to_string: (x: TTree) => string;
  of_string: (x: string) => TTree;
}

export function marshal<TTree>(e: Evaluator<TTree>): Marshaller<TTree> {
  const t_false = e.leaf;
  const t_true = e.stem(e.leaf);
  const to_bool = e.triage(() => false, _ => true, _ => raise('tree is not a bool'));
  const of_bool = (b: boolean) => b ? t_true : t_false;
  const to_list = (t: TTree) => { let l: TTree[] = []; const triage = e.triage(() => false, _ => raise('tree is not a list'), (hd, tl) => (l.push(hd), t = tl, true)); while (triage(t)); return l; };
  const of_list = (l: TTree[]) => { let f = e.leaf; for (let i = l.length; i; i--) f = e.fork(l[i - 1], f); return f; };
  const to_nat = (t: TTree): bigint => to_list(t).reduceRight((acc, b) => 2n * acc + (to_bool(b) ? 1n : 0n), 0n);
  const of_nat = (n: bigint) => { let l = []; for (; n; n >>= 1n) l.push(of_bool(n % 2n == 1n)); return of_list(l); };
  const to_string = (t: TTree) => to_list(t).map(to_nat).map(x => String.fromCharCode(Number(x))).join('');
  const of_string = (s: string) => of_list(s.split('').map(c => of_nat(BigInt(c.charCodeAt(0)))));
  return {
    to_bool,
    of_bool,
    to_list,
    of_list,
    to_nat,
    of_nat,
    to_string,
    of_string
  };
}
