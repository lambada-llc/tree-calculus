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
  existsSync, mkdtempSync, openSync, readdirSync, readSync, rmSync, statSync,
  writeFileSync, writeSync,
} from "fs";
import { tmpdir } from "os";
import { dirname, join, resolve } from "path";
import { Evaluator, raise } from "../common.mjs";
import formatter_dag from "../format/dag.mjs";
import { EnvOptions } from "../module/env.mjs";
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

/** Build the runner next to its source, unless a current binary is already there. */
const executable = once(() => {
  const from = source();
  const exe = join(dirname(from), 'runner.exe');
  const current = existsSync(exe) && statSync(exe).mtimeMs >= source_mtime(from);
  if (!current) {
    execFileSync(process.env.CXX ?? 'c++',
      ['-O3', '-std=c++17', '-pthread', from, '-o', exe], { stdio: 'inherit' });
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

/** `transformer`, with the application and the reduction it needs done natively. */
function transformer<TTree>(
  _: Evaluator<TTree>,
  program: string,
  options: TransformerOptions = {},
): (input: string) => string {
  const path = once(() => as_file(program, 'program.dag'));
  const symbol = once(() => terminator(program));
  return memoize((input: string) => {
    const payload = Buffer.from(input, 'utf8');
    return ask(path(), `apply ${symbol()} ${payload.length}`, payload).toString('utf8');
  }, program, options);
}

/** `environment`, reducing each symbol natively and reading the value back as a DAG. */
function environment<TTree>(
  e: Evaluator<TTree>,
  text: string,
  _: EnvOptions = {},
): (symbol: string) => TTree {
  const path = once(() => as_file(text, 'module.dag'));
  return (symbol: string) => formatter_dag.of(e, ask(path(), `eval-dag ${symbol}`).toString('utf8'));
}

/**
 * The native implementations, or null to stay in Node.
 *
 * Opt in with TREE_CALCULUS_RUNNER=1. It is off by default because it turns a
 * package that needs nothing but Node into one that needs a C++ compiler and
 * POSIX FIFOs.
 */
export const native = process.env.TREE_CALCULUS_RUNNER === '1'
  ? { transformer, environment }
  : null;
