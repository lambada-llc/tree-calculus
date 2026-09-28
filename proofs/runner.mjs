// Talking to `runner -s` (`implementation/cpp/dag-machine/runner.md`), and to `lake exe machine`,
// which speaks the same protocol: the eager runner built from this repository's `runner.cpp`, and
// one server process fed a batch of requests, its replies parsed back.

import { execFileSync, spawnSync } from 'node:child_process';
import { readdirSync, statSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));

/** The eager runner, rebuilt when `runner.cpp` or an evaluator header it includes is newer than
 * it — the rule the runtime's own on-demand build (native.mts) goes by. */
export function eagerRunner() {
  const cpp = resolve(here, '../implementation/cpp');
  const source = join(cpp, 'dag-machine/runner.cpp');
  const runner = join(here, 'jets/.runner-eager');
  const mtime = (path) => { try { return statSync(path).mtimeMs; } catch { return 0; } };
  const headers = readdirSync(cpp).filter((name) => name.endsWith('.hpp')).map((name) => join(cpp, name));
  if (mtime(runner) < Math.max(...[source, ...headers].map(mtime)))
    execFileSync(process.env.CXX ?? 'c++',
      ['-O2', '-std=c++17', '-pthread', '-DRUNNER_EAGER', '-o', runner, source]);
  return runner;
}

/** A `reduce dag` request for `payload`. */
export const reduceDag = (payload) => `reduce dag ${Buffer.byteLength(payload)}\n${payload}`;

/** Run `argv` as a server fed `requests`, one process for all of them. Its replies, in order —
 * `{ ok: true }`, `{ err }` or `{ data }` — as many as it gave before it exited, and its stderr;
 * `failure` says why it exited, if not by being done. */
export function session(argv, requests, options = {}) {
  const run = spawnSync(argv[0], argv.slice(1),
    { input: requests.join(''), maxBuffer: 1 << 30, ...options });
  const failure = run.error?.message ?? (run.signal ? `killed by ${run.signal}`
    : run.status ? `exit status ${run.status}` : undefined);
  const stdout = run.stdout ?? Buffer.alloc(0);
  const replies = [];
  for (let at = 0; ;) {
    const eol = stdout.indexOf(10, at);
    if (eol < 0) break;
    const head = stdout.subarray(at, eol).toString();
    at = eol + 1;
    if (head === 'ok') replies.push({ ok: true });
    else if (head.startsWith('err ')) replies.push({ err: head.slice(4) });
    else if (head.startsWith('data ')) {
      const end = at + Number(head.slice(5));
      if (end > stdout.length) break;
      replies.push({ data: stdout.subarray(at, end).toString() });
      at = end;
    } else throw new Error(`${argv[0]}: unexpected reply ${JSON.stringify(head)}`);
  }
  return { replies, stderr: run.stderr?.toString() ?? '', failure };
}
