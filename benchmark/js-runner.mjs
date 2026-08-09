// Reduce with one named JavaScript evaluator, so the suite can time them
// against each other rather than only against the other languages.
//
//   node js-runner.mjs --evaluator lazy-stacks < trees.ternary
//
// Reads one ternary tree per line and applies them left to right, starting from
// the identity, then prints the result as ternary -- the same protocol the C++
// runner speaks, so both get timed on the same input the same way.
//
// This reads the evaluators as built by `npm run build` in the TypeScript
// implementation, which is why it is a plain script here rather than a file
// there: that directory's build output is not checked in.

import { readFileSync } from 'fs';

const args = process.argv.slice(2);
const i = args.indexOf('--evaluator');
const name = i < 0 ? null : args[i + 1];
if (!name) {
  console.error('usage: js-runner.mjs --evaluator <name> < trees.ternary');
  process.exit(2);
}

const SRC = new URL('../implementation/typescript/src/', import.meta.url);
const evaluator_url = new URL(`evaluator/${name}.mjs`, SRC);
const mod = await import(evaluator_url.href).catch(() => {
  console.error(`no built evaluator ${name}; run \`npm run build\` in implementation/typescript`);
  process.exit(2);
});
// some evaluators are factories over their own backing store
const e = typeof mod.default === 'function' ? mod.default() : mod.default;
const ternary = (await import(new URL('format/ternary.mjs', SRC).href)).default;

let result = ternary.of(e, '21100'); // identity
for (const line of readFileSync(0, 'utf8').split('\n')) {
  const tree = line.trim();
  if (tree) result = e.apply(result, ternary.of(e, tree));
}
console.log(ternary.to(e, result));
