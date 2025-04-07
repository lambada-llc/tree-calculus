import { children, Evaluator, raise } from "../common.mjs";
import { Formatter } from "./formatter.mjs";

// Format:
// a b c  can be thought of as "let a = b c in" and
// a b    as "let a = b in" and
// a      terminates parsing, returning "a"

function to<TTree>(e: Evaluator<TTree>, x: TTree): string {
  const res = [];
  let i = 0;
  const app_keys: { [app_key: string]: string } = {};
  const apply_keys = (a: string, b: string) => {
    const app_key = `${a} ${b}`;
    const alloc = () => {
      const x = `${i++}`;
      res.push(`${x} ${app_key}`);
      return x;
    };
    return app_keys[app_key] ?? (app_keys[app_key] = alloc());
  };
  const keys = new Map<TTree, string>();
  const todo = [{ node: x, enter: true }];
  while (todo.length) {
    const { node, enter } = todo.pop()!;
    if (enter) {
      todo.push({ node, enter: false });
      for (const c of children(e, node))
        todo.push({ node: c, enter: true });
    } else {
      let current = '△';
      for (const c of children(e, node))
        current = apply_keys(current, keys.get(c)!);
      keys.set(node, current);
    }
  }
  res.push(keys.get(x));
  return res.join('\n');
}

function of<TTree>(e: Evaluator<TTree>, s: string): TTree {
  const env: { [name: string]: TTree } = { '△': e.leaf };
  const get_env = (name: string) => name in env ? env[name] : raise(`unbound variable: ${name}`);
  for (const line of s.split(/\r?\n/)) {
    const [a, b, c] = line.split(' ');
    if (c) env[a] = e.apply(get_env(b), get_env(c));
    else if (b) env[a] = get_env(b);
    else if (a) return get_env(a);
  }
  return raise('dag representation was unepxectedly not terminated by a value');
}

const formatter: Formatter = { to, of };
export default formatter;
