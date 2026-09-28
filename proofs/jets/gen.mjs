#!/usr/bin/env node
// The demo jets' trees: each one's eager normal form, as the base toolchain computes it, written
// to `<name>.dag` here and, through ../dag2lean.mjs, to ../TreeCalculus/Jets/Trees.lean.
//
//   LAMBADA=<lambada checkout> node proofs/jets/gen.mjs
//
// A tree is either a LambAda expression, compiled by `lambada emit` against the library
// `compile_file.dag` carries, or a symbol of `compile_file.dag` itself — the compiler's own
// trees, which is what a runtime jet is keyed on. Either way the eager runner built from this
// repository's `runner.cpp` normalizes it (`reduce dag`), with that module loaded.

import { execFileSync } from 'node:child_process';
import { statSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const trees = {
  newline: { lamb: '10' },
  carriageReturn: { lamb: '13' },
  eqConstNewline: { lamb: 'equal_const 10' },
  skipLine: { lamb: 'fix $ \\self List.match [] (\\h Bool.match self id (equal_const 10 h))' },
  isHash: { symbol: 'Lambada._is_hash:28' },
  hash: { lamb: "'#'" },
  skipComment: { symbol: 'Lambada._skip_comment:27' },
};

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../..');
const lambada = process.env.LAMBADA ?? (console.error('set LAMBADA to a lambada checkout'), process.exit(2));
const library = join(lambada, 'compiler/compile_file.dag');

const source = join(root, 'implementation/cpp/dag-machine/runner.cpp');
const runner = join(here, '.runner-eager');
const mtime = (path) => { try { return statSync(path).mtimeMs; } catch { return 0; } };
if (mtime(runner) < mtime(source))
  execFileSync(process.env.CXX ?? 'c++', ['-O2', '-std=c++17', '-pthread', '-DRUNNER_EAGER', '-o', runner, source]);

/** `runner -s`, one request: `expr`'s normal form, with the library loaded. */
function normalize(expr) {
  const request = `load ${library}\nreduce dag ${Buffer.byteLength(expr)}\n${expr}quit\n`;
  const out = execFileSync(runner, ['-s'], { input: request }).toString();
  const reply = /^ok\ndata (\d+)\n/.exec(out) ?? (() => { throw new Error(out); })();
  return Buffer.from(out.slice(reply[0].length)).subarray(0, +reply[1]).toString() + '\n';
}

const emit = (lamb) => execFileSync('node', [join(lambada, 'bin/lambada.js'), 'emit', '--tree-calculus', root,
  '--cache', join(here, '.cache')], { input: lamb + '\n' }).toString();

for (const [name, tree] of Object.entries(trees))
  writeFileSync(join(here, `${name}.dag`), normalize(tree.lamb ? emit(tree.lamb) : tree.symbol + '\n'));

writeFileSync(join(root, 'proofs/TreeCalculus/Jets/Trees.lean'), execFileSync('node', [join(here, '../dag2lean.mjs'),
  'TreeCalculus.Jets', ...Object.keys(trees).map((name) => `${name}=${join(here, name + '.dag')}`)]));
