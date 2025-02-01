export type F = F[]; // = △ <array entries in reverse order>

const reduceOne = (todo: F[]): void => {
  const f = todo.pop();
  if (f.length < 3) return;
  todo.push(f);
  const a = f.pop(), b = f.pop(), c = f.pop();
  if (a.length === 0) f.push(...b); // leaf
  else if (a.length === 1) { // stem
    const newPotRedex = [c, ...b];
    f.push(newPotRedex, c, ...a[0]);
    todo.push(newPotRedex);
  }
  else if (a.length === 2) // fork
    if (c.length === 0) f.push(...a[1]); // leaf
    else if (c.length === 1) f.push(c[0], ...a[0]); // stem
    else if (c.length === 2) f.push(c[0], c[1], ...b); // fork
};

function reduce(expression: F): F { // assumes all but top level of expression is already fully reduced!
  const todo = [expression];
  while (todo.length)
    reduceOne(todo);
  return expression;
}

export const apply = (a: F, b: F) => reduce([b, ...a]);

// construct
export const leaf: F = [];
export const stem = (u: F) => [u];
export const fork = (u: F, v: F) => [v, u];

// marshalling
export const to_bool = f => !!f?.length;
export const of_bool = b => b ? stem(leaf) : leaf;
export const to_list = f => { let l = []; while (f?.length) { l.push(f[1]); f = f[0]; } return l };
export const of_list = l => { let f = leaf; for (let i = l.length; i; i--) f = fork(l[i - 1], f); return f };
export const to_nat = f => to_list(f).reduceRight((acc, b) => 2 * acc + (to_bool(b) ? 1 : 0), 0);
export const of_nat = n => { let l = []; for (; n; n >>= 1) l.push(of_bool(n % 2)); return of_list(l) };
export const to_string = f => to_list(f).map(to_nat).map(x => String.fromCharCode(x)).join('');
export const of_string = s => of_list(s.split('').map(c => of_nat(c.charCodeAt(0))));

