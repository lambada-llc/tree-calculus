// Handing reduction to the C++ runner.
//
// `implementation/cpp/dag-machine/runner.cpp` reduces the same trees the
// evaluator in this package does, in bounded memory and without a JavaScript
// engine underneath. It ships as source, not as a binary, so using it means
// compiling it first — on demand, next to itself, the way the other C++
// implementations here are built.
//
// Only two operations are worth handing over, and they happen to be the two
// that reduce anything: turning a program into a text-to-text function
// (`transformer`) and evaluating a module's symbols (`environment`). Both keep
// their signatures, so nothing downstream can tell the difference — the choice
// between this and Node is made once, in ../main-dag.mts.

import { execFileSync, spawn } from "child_process";
import {
  existsSync, mkdtempSync, openSync, readdirSync, readFileSync, readSync,
  renameSync, rmSync, statSync, writeFileSync, writeSync,
} from "fs";
import { tmpdir } from "os";
import { dirname, join, resolve } from "path";
import { Evaluator, raise } from "../common.mjs";
import formatter_dag from "../format/dag.mjs";
import { MODULE_STORE, REDUCE_STORE, Store, store, text_key } from "../module/cache.mjs";
import { EnvOptions, Environment } from "../module/env.mjs";
import { fingerprint } from "../module/fingerprint.mjs";
import { memoize, TransformerOptions } from "../module/transform.mjs";

const SOURCE = 'implementation/cpp/dag-machine/runner.cpp';

/** Compute once, on first use. Nothing here is worth paying for unused. */
function once<T>(f: () => T): () => T {
  let value: T | undefined;
  return () => value === undefined ? (value = f()) : value;
}

/** A directory to keep this process's runner scratch in, cleaned up on the way out. */
const scratch = once(() => {
  const directory = mkdtempSync(join(tmpdir(), 'tree-calculus-'));
  process.on('exit', () => rmSync(directory, { recursive: true, force: true }));
  return directory;
});

/** The runner's source, wherever this module ended up being run or bundled from. */
function source(): string {
  for (let directory = __dirname; ; directory = dirname(directory)) {
    const candidate = join(directory, SOURCE);
    if (existsSync(candidate)) return candidate;
    if (dirname(directory) === directory) raise(`no ${SOURCE} above ${__dirname}`);
  }
}

/**
 * When `runner.cpp` was last touched — counting the evaluator headers it
 * includes, which is where nearly all of it actually lives.
 */
function source_mtime(from: string): number {
  const headers = resolve(dirname(from), '..');
  return readdirSync(headers)
    .filter(name => name.endsWith('.hpp'))
    .map(name => statSync(join(headers, name)).mtimeMs)
    .reduce((a, b) => Math.max(a, b), statSync(from).mtimeMs);
}

/**
 * Whether to build the runner's eager evaluator, which is faster but only
 * terminates on a module whose every binding has a normal form. Opting in is a
 * claim about the module, so it is the caller's to make: TREE_CALCULUS_RUNNER=eager.
 */
const eager = () => process.env.TREE_CALCULUS_RUNNER === 'eager';

/** Build the runner next to its source, unless a current binary is already there. */
const executable = once(() => {
  const from = source();
  // The two evaluators get their own binaries, so a repository that uses one
  // does not force a rebuild on a repository that uses the other.
  const exe = join(dirname(from), eager() ? 'runner-eager.exe' : 'runner.exe');
  const current = existsSync(exe) && statSync(exe).mtimeMs >= source_mtime(from);
  if (!current) {
    execFileSync(process.env.CXX ?? 'c++',
      ['-O3', '-std=c++17', '-pthread', ...(eager() ? ['-DRUNNER_EAGER'] : []),
       from, '-o', exe], { stdio: 'inherit' });
  }
  return exe;
});

/**
 * A runner in server mode, talked to over a pair of FIFOs.
 *
 * One long-lived process rather than one per request, because a request is not
 * where the interesting state lives: the module stays parsed between commands,
 * and reduction is in place, so every evaluation leaves the trees it touched
 * further along for the next one.
 *
 * FIFOs rather than the pipes `spawn` would set up, because everything in this
 * library is synchronous and Node only offers those asynchronously. Opened
 * read-write so that neither end blocks waiting for the other and neither side
 * ever reads EOF, `readSync` blocks until the runner answers — which is exactly
 * the behaviour a synchronous client wants.
 */
const server = once(() => {
  const to = join(scratch(), 'to-runner');
  const from = join(scratch(), 'from-runner');
  execFileSync('mkfifo', [to, from]);
  const write_fd = openSync(to, 'r+');
  const read_fd = openSync(from, 'r+');
  const runner = spawn(executable(), ['-s'], { stdio: [write_fd, read_fd, 'inherit'] });
  // Waiting for the runner is never what keeps this process alive: it only ever
  // has something to say in response to being asked, and it is asked
  // synchronously.
  runner.unref();
  process.on('exit', () => runner.kill());

  const byte = Buffer.alloc(1);
  const line = (): string => {
    let text = '';
    for (; ;) {
      if (readSync(read_fd, byte, 0, 1, null) === 0) raise('runner: no response');
      if (byte[0] === 10) return text;
      text += String.fromCharCode(byte[0]);
    }
  };
  const bytes = (length: number): Buffer => {
    const buffer = Buffer.alloc(length);
    for (let got = 0; got < length;) got += readSync(read_fd, buffer, got, length - got, null);
    return buffer;
  };

  return (command: string, payload?: Buffer): Buffer => {
    writeSync(write_fd, `${command}\n`);
    if (payload) writeSync(write_fd, payload);
    const head = line();
    if (head === 'ok') return Buffer.alloc(0);
    if (!head.startsWith('data ')) raise(`runner: ${head.replace(/^err /, '')}`);
    return bytes(Number(head.slice('data '.length)));
  };
});

/** Whichever module the runner is holding, so it is only ever re-read when it changes. */
let loaded: string | null = null;

/** Ask the runner something about `path`, loading it first if it is not already. */
function ask(path: string, command: string, payload?: Buffer): Buffer {
  const send = server();
  if (loaded !== path) {
    loaded = null;
    send(`load ${path}`);
    loaded = path;
  }
  return send(command, payload);
}

/**
 * Reduce `expression` — DAG text ending in the value wanted — against `path`,
 * rendered as `format`.
 *
 * The runner takes one kind of question, so this is the only shape a question
 * has here: a symbol is a one-word DAG, and applying something to host data is
 * `bound` below plus a payload that mentions it.
 */
function reduced(path: string, format: 'dag' | 'string', expression: string): Buffer {
  const payload = Buffer.from(expression, 'utf8');
  return ask(path, `reduce ${format} ${payload.length}`, payload);
}

/**
 * `text` as a tree-calculus string, under a name the next `reduced` can use.
 *
 * `~` because a module never exports such a name — it is what the runner's own
 * dumps use for scaffolding, for the same reason.
 */
function bound(path: string, name: string, text: string): string {
  const payload = Buffer.from(text, 'utf8');
  ask(path, `bind ${name} ${payload.length}`, payload);
  return name;
}

/** What a DAG ends on: the value `load` leaves in the environment under that name. */
function terminator(text: string): string {
  const lines = text.split(/\r?\n/).map(line => line.trim()).filter(Boolean);
  const words = (lines[lines.length - 1] ?? '').split(/\s+/);
  return words.length === 1
    ? words[0]
    : raise('dag representation was unexpectedly not terminated by a value');
}

/** Somewhere the runner can read `text` from: it loads modules by path, not by pipe. */
function as_file(text: string, name: string): string {
  const path = join(scratch(), name);
  writeFileSync(path, text);
  return path;
}

// A dump's sidecar pairs each term fingerprint of the module that was dumped
// with the dump's id for its value — which is what lets the *next* version of
// the module skip re-evaluating every binding it did not change (see `delta`).
const SIDECAR_STORE = 'module-fp-v1';

/** The last few module dumps written here, most recent first. */
function recent_dumps(modules: Store): { list: string[], remember(key: Buffer): void } {
  const at = join(dirname(modules.path(text_key(''))), 'RECENT');
  const list = existsSync(at)
    ? readFileSync(at, 'utf8').split('\n').filter(Boolean)
    : [];
  return {
    list,
    remember(key) {
      const next = [key.toString('hex'), ...list.filter(k => k !== key.toString('hex'))];
      const temporary = `${at}.${process.pid}.tmp`;
      writeFileSync(temporary, next.slice(0, 8).join('\n') + '\n');
      renameSync(temporary, at);
    },
  };
}

/**
 * `text`, with every binding whose term a previous dump already evaluated
 * turned into an alias into that dump — or null when no previous dump covers
 * enough of it to bother.
 *
 * The returned module means exactly what `text` means: an alias replaces a
 * binding only when the fingerprints say it is the same term, and a term's
 * normal form does not care who computed it. What changed since the last
 * build is evaluated for real; the rest is a lookup. The dump's structural
 * lines come along in full — unreferenced ones cost arena nodes, not
 * correctness — and its own aliases are dropped, so no name from the old
 * module survives into the new one.
 */
function delta(modules: Store, text: string, fingerprints: Map<string, Buffer>): string | null {
  const sidecars = store(SIDECAR_STORE);
  if (!sidecars) return null;
  const lines = text.split('\n');

  // A name bound twice means a name whose fingerprint depends on where in the
  // module one asks — bail rather than alias the wrong occurrence. (Nothing
  // that writes bundles writes shadowed ones; `disambiguate` exists for this.)
  const definitions = lines.filter(line => line.split(' ').filter(Boolean).length >= 2).length;
  if (definitions !== fingerprints.size) return null;

  for (const key of recent_dumps(modules).list) {
    const id = Buffer.from(key, 'hex');
    const sidecar = sidecars.get(id);
    if (!sidecar || !modules.has(id)) continue;
    const value_of = new Map<string, string>(); // fingerprint hex -> dump ref
    for (const line of sidecar.toString('utf8').split('\n')) {
      const [fp, ref] = line.split(' ');
      if (ref) value_of.set(fp, ref);
    }

    let bindings = 0;
    let matched = 0;
    const out: string[] = [];
    for (const line of lines) {
      const words = line.split(' ').filter(Boolean);
      const known = words.length >= 2
        ? value_of.get(fingerprints.get(words[0])?.toString('hex') ?? '')
        : undefined;
      if (words.length >= 2) bindings++;
      if (known === undefined) {
        out.push(line);
      } else {
        matched++;
        out.push(`${words[0]} ${known}`);
      }
    }
    if (matched * 2 < bindings) continue; // mostly new: aliasing buys too little

    const dump = modules.get(id)!.toString('utf8');
    const structure = dump.split('\n').filter(line => line.startsWith('~'));
    return structure.join('\n') + '\n' + out.join('\n');
  }
  return null;
}

/** The sidecar `delta` reads: this module's term fingerprints, joined to the dump's ids. */
function sidecar_of(dump: Buffer, fingerprints: Map<string, Buffer>): string {
  const ref_of = new Map<string, string>(); // module name -> dump ref
  for (const line of dump.toString('utf8').split('\n')) {
    const words = line.split(' ');
    if (words.length === 2 && !words[0].startsWith('~')) ref_of.set(words[0], words[1]);
  }
  const out: string[] = [];
  for (const [name, fp] of fingerprints) {
    const ref = ref_of.get(name);
    if (ref) out.push(`${fp.toString('hex')} ${ref}`);
  }
  return out.join('\n') + '\n';
}

/**
 * Where the runner should read a module from: the cached, fully evaluated dump
 * when there is one, else the text itself — in which case the module is loaded
 * now (against the pieces of the previous dump it can reuse, when it can),
 * dumped, and the dump kept, so evaluating its bindings is paid once per
 * module *version* rather than once per process that reads it — a cache
 * warmer's workers, every later build of an unchanged module, and every
 * binding the next version of the module leaves alone.
 *
 * Eager only: the dump renders each binding's normal form, which is exactly
 * what the lazy evaluator is there not to insist on. And opt-in like every
 * cache here: TREE_CALCULUS_CACHE unset means nothing below changes hands.
 */
function loadable(text: string, name: string): () => string {
  return once(() => {
    const modules = eager() ? store(MODULE_STORE) : null;
    // A `~` name in the module itself would be indistinguishable from a
    // dump's scaffolding on the way back in; such a module (none of the
    // tooling writes one) is simply not cached.
    if (!modules || /^~/m.test(text)) return as_file(text, name);
    const key = text_key(text);
    if (modules.has(key)) return modules.path(key); // the entry is itself a loadable module
    const raw = as_file(text, name);
    try {
      const fingerprints = fingerprint(text).fingerprints;
      let used = raw;
      let dump: Buffer;
      try {
        const patched = delta(modules, text, fingerprints);
        if (patched !== null) used = as_file(patched, `delta-${name}`);
        dump = ask(used, 'dump');
      } catch (error) {
        if (used === raw) throw error;
        used = raw; // the delta was the problem; the text itself is authoritative
        dump = ask(raw, 'dump');
      }
      const final = modules.put(key, dump);
      store(SIDECAR_STORE)?.put(key, sidecar_of(dump, fingerprints));
      recent_dumps(modules).remember(key);
      if (loaded === used) loaded = final; // same bindings: no need to re-load
      return final;
    } catch {
      return raw; // e.g. a runner without `dump`; just load the text every time
    }
  });
}

/**
 * `miss()`'s answer, kept in `named` under the term fingerprint `key` computes.
 *
 * A fingerprint addresses a term, reduction is deterministic, and the answers
 * cached here are renderings of a term's normal form — so an entry written by
 * any build, any process, either evaluator, is the answer. `key` throwing (a
 * name the fingerprints cannot see) just means this one is not cacheable.
 */
function answered(
  named: string,
  key: () => Buffer | undefined,
  miss: () => Buffer,
): Buffer {
  const st = store(named);
  if (!st) return miss();
  let fp: Buffer | undefined;
  try { fp = key(); } catch { fp = undefined; }
  if (!fp) return miss();
  const hit = st.get(fp);
  if (hit) return hit;
  const answer = miss();
  st.put(fp, answer);
  return answer;
}

/** `transformer`, with the application and the reduction it needs done natively. */
function transformer<TTree>(
  _: Evaluator<TTree>,
  program: string,
  options: TransformerOptions = {},
): (input: string) => string {
  const path = loadable(program, 'program.dag');
  const symbol = once(() => terminator(program));
  return memoize((input: string) => {
    const argument = bound(path(), '~input', input);
    return reduced(path(), 'string', `~result ${symbol()} ${argument}\n~result\n`)
      .toString('utf8');
  }, program, options);
}

/** `environment`, reducing each symbol natively and reading the value back as a DAG. */
function environment<TTree>(
  e: Evaluator<TTree>,
  text: string,
  _: EnvOptions = {},
): Environment<TTree> {
  const path = loadable(text, 'module.dag');
  const of_answer = (answer: Buffer) => formatter_dag.of(e, answer.toString('utf8'));
  // Fingerprints address the terms asked about, so their answers can be kept
  // (see `answered`). Lazy: a run whose every answer is already on disk never
  // fingerprints the module, spawns the runner, or evaluates a thing.
  const fingerprints = once(() => fingerprint(text).fingerprints);
  // A symbol is a DAG of one word, so both halves of this interface are the
  // same request with a different payload.
  const get = (symbol: string) => of_answer(answered(
    REDUCE_STORE,
    () => fingerprints().get(symbol),
    () => reduced(path(), 'dag', `${symbol}\n`)));
  // The runner reads the payload in a scope of its own, so this leaves the
  // loaded module exactly as it found it — see `reduce` in runner.cpp.
  get.reduce = (text: string) => of_answer(answered(
    REDUCE_STORE,
    () => fingerprint(text, name => fingerprints().get(name)).value,
    () => reduced(path(), 'dag', text)));
  return get;
}

/**
 * The native implementations, or null to stay in Node.
 *
 * Opt in with TREE_CALCULUS_RUNNER=1, or =eager for the faster evaluator that
 * requires every binding in the module to have a normal form (see `eager`
 * above). Off by default because it turns a package that needs nothing but Node
 * into one that needs a C++ compiler and POSIX FIFOs.
 */
export const native = ['1', 'eager'].includes(process.env.TREE_CALCULUS_RUNNER ?? '')
  ? { transformer, environment }
  : null;
