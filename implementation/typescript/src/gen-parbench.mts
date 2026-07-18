// Generator for the "parallel-and" benchmark: a program whose reduction is
// dominated by many *independent* expensive computations combined into a single
// bit, so it exercises a parallel reducer far better than fib/silly-exp.
//
// program = \n. and(and(P, P), and(P, P), ...)   -- a balanced AND of WIDTH copies
//   where P = equal (exp n) (exp n)              -- expensive, independent, = true
//
// Why this shape (see benchmark/BREAKING-RECORDS.md §4):
//   - Each P builds two 2^n trees and compares them: substantial, and the two
//     builds plus the WIDTH copies are all independent -> lots of parallel work.
//   - The combine is a *balanced* AND tree of cheap boolean ops (log depth), not
//     a sequential fold, so recombination is negligible.
//   - The output is a single bit (true), so there is no giant tree to print --
//     unlike silly-exp, whose 2^n-leaf output dilutes the parallel win.
//   - No fixpoint of its own (exp is a given finite program), so it renders to a
//     finite tree by abstraction elimination over n.
//
// Knobs (env): WIDTH = number of independent leaves (parallel breadth),
//              and the runtime input n = per-leaf work (2^n).
//
// Usage:
//   esbuild src/gen-parbench.mts --bundle --platform=node --format=esm --outfile=g.mjs
//   WIDTH=16 node g.mjs emit        # print the program in ternary
//   WIDTH=16 node g.mjs selftest    # check it returns true for small n

import { abs, app, variable, node, marshal_term, type Term_Lambda } from './abstraction-elimination/term.mjs';
import { kiselyov_kopt } from './abstraction-elimination/strategies.mjs';
import e from './evaluator/lazy-stacks.mjs';
import formatter_ternary from './format/ternary.mjs';
import { marshal } from './common.mjs';
import { equal_ternary, silly_exp_ternary } from './example-programs.mjs';

const m = marshal(e);
const compileTree = (t: Term_Lambda) => marshal_term(e)(kiselyov_kopt(t));
const toTernary = (t: Term_Lambda) => formatter_ternary.to(e, compileTree(t));

// Lift a ternary program string into a closed lambda term (node / app only).
function ternaryToTerm(s: string): Term_Lambda {
  const st: Term_Lambda[] = [];
  for (let i = s.length - 1; i >= 0; i--) {
    const c = s[i];
    if (c === '0') st.push(node);
    else if (c === '1') { const u = st.pop()!; st.push(app(node, u)); }
    else if (c === '2') { const u = st.pop()!, v = st.pop()!; st.push(app(node, u, v)); }
  }
  return st[st.length - 1];
}

const KK = abs('x', abs('y', variable('x')));                 // K = \x y. x
const falseT = node;                                          // leaf
// ifte c t f : triage on c -> leaf(false)->f | stem(_)->t | fork->unused
const ifte = (c: Term_Lambda, t: Term_Lambda, f: Term_Lambda): Term_Lambda =>
  app(node, app(node, f, app(KK, t)), node, c);
const and = (a: Term_Lambda, b: Term_Lambda): Term_Lambda => ifte(a, b, falseT);
const balancedAnd = (xs: Term_Lambda[]): Term_Lambda =>
  xs.length === 1 ? xs[0]
    : and(balancedAnd(xs.slice(0, xs.length >> 1)), balancedAnd(xs.slice(xs.length >> 1)));

const expT = ternaryToTerm(silly_exp_ternary);
const equalT = ternaryToTerm(equal_ternary);
const WIDTH = Number(process.env.WIDTH ?? '16');

// leaf predicate over the runtime input n: equal (exp n) (exp n) -> true
const leaf = (): Term_Lambda => app(equalT, app(expT, variable('n')), app(expT, variable('n')));
const program = abs('n', balancedAnd(Array.from({ length: WIDTH }, leaf)));

const mode = process.argv[2] ?? 'selftest';
if (mode === 'emit') {
  console.log(toTernary(program));
} else {
  const prog = compileTree(program);
  for (const n of [1, 2, 3, 4]) {
    const out = e.apply(prog, m.of_nat(BigInt(n)));
    console.log(`prog(${n}) = ${m.to_bool(out)}  (WIDTH=${WIDTH})`);
  }
}
