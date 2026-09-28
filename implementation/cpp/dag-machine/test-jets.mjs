#!/usr/bin/env node
// The eager runner's jets (../jets.hpp) against the pure-Node evaluator: skip_line applied to
// lists built to catch a native loop out — every tree it must not take for a newline, stems in
// the spine, arguments that are no list at all — answers the same with jets, without them, and
// in Node. RUNNER_STATS shows the jet fired, so agreement is not agreement of two reductions.
//
//   node test-jets.mjs <runner-eager.exe>

import { execFileSync, spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const runner = process.argv[2];
const root = join(dirname(fileURLToPath(import.meta.url)), '../../..');
const scratch = mkdtempSync(join(tmpdir(), 'tc-jets-test-'));
process.on('exit', () => rmSync(scratch, { recursive: true, force: true }));

// Trees, and a module that names them: hash-consed DAG lines, one per node.
const leaf = { id: '△', pair: false };
const lines = [];
const nodes = new Map();
const node = (key, line) => nodes.get(key) ?? (nodes.set(key, { id: `n${nodes.size}`, pair: key[0] === 'f' }),
  lines.push(`${nodes.get(key).id} ${line}`), nodes.get(key));
const stem = (u) => node(`s ${u.id}`, `△ ${u.id}`);
const fork = (u, v) => node(`f ${u.id} ${v.id}`, `${stem(u).id} ${v.id}`);
const list = (xs, tail = leaf) => xs.reduceRight((t, x) => fork(x, t), tail);
const nat = (n) => list(n.toString(2).split('').reverse().map((b) => (b === '1' ? stem(leaf) : leaf)));
const bits = (...bs) => list(bs.map((b) => (b ? stem(leaf) : leaf)));
const str = (s) => list([...s].map((c) => nat(c.codePointAt(0))));

// skip_line as the proof has it, under names of its own.
const skipLine = readFileSync(join(root, 'proofs/jets/skipLine.dag'), 'utf8').trim().split('\n');
const F = [...skipLine.slice(0, -1).map((line) => line.replace(/(^|\s)(\d+)/g, '$1F$2')),
  `skip_line F${skipLine.at(-1)}`];

const nl = nat(10);
const a = nat(97);
const cases = {
  empty: leaf,
  'newline only': list([nl]),
  'newline first': list([nl, a, a]),
  'newline last': list([a, a, nl]),
  'the first of several newlines': str('ab\ncd\nef'),
  'no newline': str('no newline at all'),
  // Jets.lean: to skip_line, a carriage return is a character like any other.
  'carriage return': str('a\rb\nc'),
  // 10 is [0, 1, 0, 1]: what shares a prefix with it, or is it but for one more bit, is not it.
  '[0, 1]': list([bits(0, 1), nl, a]),
  '[0, 1, 0]': list([bits(0, 1, 0), nl, a]),
  '[0, 1, 0, 1, 0], 10 with a trailing zero': list([bits(0, 1, 0, 1, 0), nl, a]),
  '[0, 1, 0, 1, 1]': list([bits(0, 1, 0, 1, 1), nl, a]),
  '[1, 0, 1, 0]': list([bits(1, 0, 1, 0), nl, a]),
  '10 with a bit that is another truthy tree': list([list([leaf, stem(stem(leaf)), leaf, stem(leaf)]), nl, a]),
  '10 under a stem': list([stem(nl), nl, a]),
  '10 inside a list': list([list([nl]), nl, a]),
  "10 as a pair's tail": list([fork(a, nl), nl, a]),
  'a stem ends the spine': fork(a, stem(leaf)),
  'a stem after skipped elements': list([a, a, a], stem(nl)),
  'a stem after a newline': list([a, nl], stem(a)),
  'a stem as the argument': stem(leaf),
  'a newline under a stem as the argument': stem(list([nl])),
  'a fork of forks as the argument': fork(fork(leaf, leaf), fork(stem(leaf), nl)),
  'the newline itself, a list of bits': nl,
  'skip_line itself': null, // see `applications`
  'long, newline at the end': list([...Array(20000).fill(a), nl, a]),
  'long, no newline': list(Array(20000).fill(a)),
  'long, a stem at the end': list(Array(20000).fill(a), stem(a)),
};

// Arbitrary trees, spines ending in anything, with a newline now and then: seeded, so a failure
// is the same failure next time.
let seed = 1;
const random = (n) => (seed = (seed * 1103515245 + 12345) % 2 ** 31) % n;
const tree = (depth) => {
  const pick = depth ? random(8) : random(2);
  return pick === 0 ? leaf : pick === 1 ? nl : pick < 4 ? stem(tree(depth - 1))
    : pick < 6 ? nat(random(20)) : fork(tree(depth - 1), tree(depth - 1));
};
for (let i = 0; i < 200; ++i) {
  const xs = Array.from({ length: random(12) }, () => tree(3));
  cases[`random ${i}`] = list(xs, random(4) ? leaf : tree(2));
}

const names = Object.keys(cases);
const applications = names.map((name, i) => `r${i} skip_line ${cases[name]?.id ?? 'skip_line'}`);
let results = '△';
const cells = [];
for (let i = names.length; i-- > 0; results = `c${i}`) cells.push(`c${i}s △ r${i}`, `c${i} c${i}s ${results}`);
const path = join(scratch, 'm.dag');
writeFileSync(path, [...F, ...lines, ...applications, ...cells, `results ${results}`].join('\n') + '\n');

let pass = 0;
let fail = 0;
const check = (what, ok, detail = '') => {
  if (ok) { console.log(`PASS jets ${what}`); ++pass; }
  else { console.log(`FAIL jets ${what}${detail && `: ${detail}`}`); ++fail; }
};

// One `runner -s` session: the answers, and the RUNNER_STATS line of each command.
const session = (commands, env = {}) => {
  const out = spawnSync(runner, ['-s'], {
    input: commands, env: { ...process.env, RUNNER_STATS: '1', ...env }, maxBuffer: 1 << 30,
  });
  const answers = [];
  for (let at = 0, buf = out.stdout; at < buf.length;) {
    const eol = buf.indexOf(10, at);
    const head = buf.subarray(at, eol).toString();
    at = eol + 1;
    if (!head.startsWith('data ')) { answers.push(head); continue; }
    const n = +head.slice(5);
    answers.push(buf.subarray(at, at + n).toString());
    at += n;
  }
  return { answers, stats: out.stderr.toString().split('\n').filter(Boolean) };
};
const reduce = (expr) => `reduce dag ${Buffer.byteLength(expr)}\n${expr}`;
const stat = (line, key) => +new RegExp(`${key}=(\\d+)`).exec(line)[1];

const { TREE_CALCULUS_RUNNER, TREE_CALCULUS_CACHE, ...node_only } = process.env;
const oracle = execFileSync('node', [join(root, 'bin/dag.js'), 'eval', '--symbol', 'results',
  '--format', 'dag', path], { env: node_only, maxBuffer: 1 << 30 }).toString().trimEnd();
const on = session(`load ${path}\n${reduce('results\n')}`);
const off = session(`load ${path}\n${reduce('results\n')}`, { RUNNER_JETS: '0' });
check(`skip_line on ${names.length} arguments, jets on, matches Node`, on.answers[1] === oracle);
check('the same, jets off', off.answers[1] === oracle);
// Once per argument that is a pair: a leaf or a stem is one rule of skip_line's own.
const pairs = names.filter((name) => cases[name]?.pair ?? true).length;
check(`the jet fired on each of the ${pairs} pairs, and skipped`, stat(on.stats[0], 'jets') === pairs
  && stat(on.stats[0], 'skipped') >= 60000, on.stats[0]);
check('with jets off it did not', stat(off.stats[0], 'jets') === 0, off.stats[0]);

// The jet's trees are the evaluator's own: a collection keeps them, and a load re-interns them
// into the fresh arena. So skip_line built from a request's own payload — after collections that
// had no root reaching it, and after a second load — is still the tree the jet is keyed on.
const big = list([...Array(200000).fill(a), nl]);
const bare = join(scratch, 'bare.dag');
writeFileSync(bare, [...lines, `big ${big.id}`].join('\n') + '\n');
const request = [...skipLine.slice(0, -1), `~r ${skipLine.at(-1)} big`, '~r'].join('\n') + '\n';
const later = session(`load ${bare}\n${reduce(request)}load ${bare}\n${reduce(request)}`,
  { RUNNER_RSS_THRESHOLD_MB: '1' });
check('the jet survives collection and re-load', later.answers[1] === '△' && later.answers[3] === '△'
  && stat(later.stats[0], 'gcs') > 0 && later.stats.filter((s) => / reduce /.test(s))
    .every((s) => stat(s, 'jets') === 1 && stat(s, 'skipped') === 200000), later.stats.join('\n'));

console.log(`\njets: ${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
