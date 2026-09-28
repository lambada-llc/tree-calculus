#!/usr/bin/env node
// A differential test of what README.md trusts rather than proves: that `step` is `apply`'s
// loop. `step` itself, compiled (`lake exe machine`, Main.lean), and eager runners are handed
// the same programs, and every answer the machine reaches within its fuel must be every
// runner's, byte for byte — both print `to_dag`, which prints equal trees as equal text. A
// program the machine does not finish is counted and skipped, never compared. It samples; it
// proves nothing.
//
//   node proofs/difftest.mjs [--cases n] [--seed n] [--fuel steps] [--timeout s]
//                            [--bundle file.dag] [runner...]
//
// The runners are this repository's eager runner (built from `runner.cpp`) and any others
// named. Each serves all the programs in one process, since what a runner carries from one
// request to the next — its memo, its interned nodes — is part of what is under test; and does
// so twice, with the default collection budget and with the smallest (1 MiB), which collects,
// frees and evicts memo entries throughout: the parts of the runtime the machine leaves out.
//
// The programs, `--cases` of each kind, from a generator seeded by `--seed`:
//   tree     a random tree applied to a random tree
//   expr     a DAG of applications of random trees, K and I to each other's results
//   jets     the same over the jets' trees (jets/*.dag), characters and lists of them
//   drop     skipLine, skipComment, skipLineFix on random lists of characters and trees
//   member   isNewline, isHash, eqConstNewline on characters, near misses, random trees
//   symbol   a symbol of `--bundle` (a DAG module, e.g. arboretum's src/.dag-bundle-canonical)
//   call     a function of `--bundle` applied to one or two random arguments or symbols

import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { tmpdir } from 'node:os';
import { basename, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { eagerRunner, reduceDag, session } from './runner.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const options = { cases: 1000, seed: 1, fuel: 1_000_000, timeout: 600 };
const runners = [];
for (const args = process.argv.slice(2); args.length;) {
  const arg = args.shift();
  if (arg.startsWith('--')) options[arg.slice(2)] = args.shift();
  else runners.push(arg);
}
const [cases, fuel] = [+options.cases, +options.fuel];

// mulberry32: a generator small enough to be part of the seed's meaning.
let state = +options.seed >>> 0;
const random = () => {
  state = (state + 0x6d2b79f5) >>> 0;
  let t = Math.imul(state ^ (state >>> 15), state | 1);
  t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
  return ((t ^ (t >>> 14)) >>> 0) / 2 ** 32;
};
const below = (n) => Math.floor(random() * n);
const pick = (xs) => xs[below(xs.length)];
/** Mostly short, a tenth of the time long: long enough for a collection to fall mid-reduction. */
const length = (short, long) => below(random() < 0.1 ? long : short);

// Trees: `null` is △, `[u]` the stem △u, `[u, v]` the fork △uv. A subtree used twice is one
// object, and one node of the payload.
const K = [null];
const I = [[K], K];
const nat = (n) => n ? [n & 1 ? K : null, nat(n >>> 1)] : null;      // bits, least first
const list = (xs, tail = null) => xs.reduceRight((t, x) => [x, t], tail);
const shared = [];
const randomTree = (size) => {
  if (size <= 0) return null;
  if (shared.length && random() < 0.1) return pick(shared);
  const k = below(size);
  const t = random() < 0.3 ? [randomTree(size - 1)] : [randomTree(k), randomTree(size - 1 - k)];
  if (shared.push(t) > 64) shared.shift();
  return t;
};

/** A request's DAG under construction: lines, and the name each tree already has in it. */
class Payload {
  lines = [];
  names = new Map();
  apply(f, x) {
    const id = `~${this.lines.length}`;
    this.lines.push(`${id} ${f} ${x}`);
    return id;
  }
  /** The name `t` has here: a string is a name already, and a tree one once it is built. */
  name(t) {
    return t === null ? '△' : typeof t === 'string' ? t : this.names.get(t);
  }
  /** A tree, built: each line applies △ or a stem, so reading it reduces nothing. Children
   * first, with a stack of its own rather than JavaScript's, which a long list is deeper than. */
  tree(t) {
    for (const todo = [t]; todo.length;) {
      const u = todo.at(-1);
      const kids = this.name(u) === undefined ? u.filter((k) => this.name(k) === undefined) : [];
      if (kids.length) { todo.push(...kids); continue; }
      todo.pop();
      if (this.name(u) !== undefined) continue;
      const stem = this.apply('△', this.name(u[0]));
      this.names.set(u, u.length === 1 ? stem : this.apply(stem, this.name(u[1])));
    }
    return this.name(t);
  }
  /** Lines of a DAG, their names prefixed; the name of the value it ends on. */
  include(text, prefix) {
    const name = (w) => w === '△' ? w : `${prefix}${w}`;
    const words = text.trim().split('\n').map((line) => line.trim().split(/\s+/).map(name));
    this.lines.push(...words.filter((w) => w.length > 1).map((w) => w.join(' ')));
    return words.at(-1)[0];
  }
  /** `f` applied to each of `args` in turn: the request. */
  call(f, ...args) {
    const root = args.reduce((g, x) => this.apply(g, this.tree(x)), this.tree(f));
    return `${this.lines.join('\n')}\n${root}\n`;
  }
}

const jetTrees = Object.fromEntries(readdirSync(join(here, 'jets'))
  .filter((f) => f.endsWith('.dag'))
  .map((f) => [basename(f, '.dag'), readFileSync(join(here, 'jets', f), 'utf8')]));
const jet = (p, name) => p.include(jetTrees[name], `${name}.`);
const [newline, carriageReturn, hash] = [10, 13, 35].map(nat);

/** A tree a character predicate or a line loop may be handed: mostly characters, and near misses
 * of a newline — with a trailing zero bit, its stem, a fork on it. */
const element = () => pick([
  () => newline, () => newline, () => carriageReturn, () => hash,
  () => nat(below(128)), () => nat(below(128)), () => nat(below(0x110000)),
  () => list([null, K, null, K, null]), () => [newline], () => [newline, randomTree(4)],
  () => randomTree(below(12)),
])();

/** Random applications of `atoms` (names or trees) to each other and to their results. */
function applications(p, atoms) {
  const values = atoms.map((a) => p.tree(a));
  const lines = 1 + below(12);
  for (let i = 0; i < lines; i++) values.push(p.apply(pick(values), pick(values)));
  return `${p.lines.join('\n')}\n${values.at(-1)}\n`;
}

const bundle = options.bundle && (() => {
  const { DagModule } = createRequire(import.meta.url)('../bin/dag.js');
  const module = DagModule.parse(readFileSync(options.bundle, 'utf8'));
  const symbols = [...new Set(module.lines.filter((l) => l.length === 2 || l.length === 3)
    .map((l) => l[0].symbol).filter((s) => !/^\d+$/.test(s)))];
  const payload = (...names) => {
    const p = new Payload();
    p.lines = module.extract(...names).toString().trim().split('\n');
    return p;
  };
  return { symbols, payload };
})();

const kinds = {
  tree: () => new Payload().call(randomTree(below(24)), randomTree(below(24))),
  expr: () => applications(new Payload(),
    [K, I, ...Array.from({ length: 1 + below(4) }, () => randomTree(below(16)))]),
  jets: () => {
    const p = new Payload();
    return applications(p, [...Object.keys(jetTrees).map((name) => jet(p, name)), K, I,
      element(), element(),
      list(Array.from({ length: below(8) }, element))]);
  },
  drop: () => {
    const p = new Payload();
    const f = pick(['skipLine', 'skipComment', 'skipLineFix']);
    const xs = Array.from({ length: length(48, 4_000) }, element);
    const arg = random() < 0.1 ? randomTree(below(16))                 // no list at all
      : list(xs, random() < 0.1 ? randomTree(1 + below(8)) : null);   // or an improper one
    return p.call(jet(p, f), arg);
  },
  member: () => {
    const p = new Payload();
    return p.call(jet(p, pick(['isNewline', 'isHash', 'eqConstNewline'])), element());
  },
  ...bundle && {
    symbol: () => {
      const s = pick(bundle.symbols);
      return bundle.payload(s).call(s);
    },
    call: () => {
      const data = () => pick([
        () => nat(below(100)), () => pick([null, K]), () => randomTree(below(12)),
        () => list(Array.from({ length: length(6, 1_000) }, () => nat(below(20)))),
        () => list([...'tree calculus'.slice(below(13))].map((c) => nat(c.charCodeAt(0)))),
      ])();
      const f = pick(bundle.symbols);
      const args = Array.from({ length: 1 + below(2) },
        () => random() < 0.3 ? pick(bundle.symbols) : data());
      return bundle.payload(f, ...args.filter((a) => typeof a === 'string')).call(f, ...args);
    },
  },
};

const programs = Object.entries(kinds).flatMap(([kind, make]) =>
  Array.from({ length: cases }, () => ({ kind, dag: make() })));

/** A runner's counters (`RUNNER_STATS`), summed over its requests. */
const stats = ({ stderr }) => {
  const total = {};
  for (const [, key, n] of stderr.matchAll(/ (\w+)=(\d+)/g)) total[key] = (total[key] ?? 0) + +n;
  return total;
};

// The machine first: what it answers within its fuel is what the runners are asked.
execFileSync('lake', ['build', 'machine'], { cwd: here, stdio: ['ignore', 'ignore', 'inherit'] });
const machine = session([join(here, '.lake/build/bin/machine'), `${fuel}`],
  [...programs.map((p) => reduceDag(p.dag)), 'quit\n']);
if (machine.failure) throw new Error(`machine: ${machine.failure}`);
const steps = [...machine.stderr.matchAll(/steps=(\d+)/g)].map(([, n]) => +n);
programs.forEach((p, i) => { p.expected = machine.replies[i]; p.steps = steps[i]; });
const unexpected = programs.filter((p) => p.expected.err && p.expected.err !== 'out of fuel');
if (unexpected.length)
  throw new Error(`machine: ${unexpected[0].expected.err}\n${unexpected[0].dag}`);
const answered = programs.filter((p) => p.expected.data !== undefined);

console.log(`seed ${options.seed}, fuel ${fuel} steps a program; the machine's answers took`
  + ` ${answered.reduce((n, p) => n + p.steps, 0)} steps\n`);
const row = (kind, ...counts) => kind.padEnd(8) + counts.map((c) => `${c}`.padStart(12)).join('');
console.log(row('kind', 'programs', 'compared', 'out of fuel'));
for (const kind of Object.keys(kinds)) {
  const of = programs.filter((p) => p.kind === kind);
  const n = of.filter((p) => p.expected.data !== undefined).length;
  console.log(row(kind, of.length, n, of.length - n));
}
console.log();

let failed = false;
let saved;
const save = (name, text) => {
  saved ??= mkdtempSync(join(tmpdir(), 'difftest-'));
  writeFileSync(join(saved, name), text);
};
for (const runner of [eagerRunner(), ...runners]) {
  for (const budget of [undefined, '1']) {
    const env = { ...process.env, RUNNER_STATS: '1' };
    if (budget) env.RUNNER_RSS_THRESHOLD_MB = budget; else delete env.RUNNER_RSS_THRESHOLD_MB;
    const run = session([runner, '-s'],
      ['load /dev/null\n', ...answered.map((p) => reduceDag(p.dag)), 'quit\n'],
      { env, timeout: options.timeout * 1000 });
    const replies = run.replies.slice(1, 1 + answered.length);
    const differ = answered.filter((p, i) => replies[i]?.data !== p.expected.data);
    const total = stats(run);
    console.log(`${runner} (budget ${budget ? `${budget} MiB` : 'default'}): `
      + `${answered.length - differ.length} agree, ${differ.length} differ`
      + ` — ${total.steps} steps, ${total.hits} memo hits, ${total.gcs} collections`);
    if (run.failure) console.log(`  stopped after ${replies.length} programs: ${run.failure}`);
    for (const p of differ.slice(0, 20)) {
      const at = answered.indexOf(p);
      save(`${at}.dag`, p.dag);
      save(`${at}.machine`, p.expected.data);
      save(`${at}.${basename(runner)}-${budget ?? 'default'}`,
        replies[at]?.data ?? `${replies[at]?.err ?? 'no answer'}\n`);
    }
    failed ||= differ.length > 0 || !!run.failure;
  }
}
if (saved) console.log(`\nprograms that differ, and the answers: ${saved}`);
process.exit(failed ? 1 : 0);
