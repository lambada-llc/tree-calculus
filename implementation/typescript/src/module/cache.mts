// The on-disk reduction cache: `.cache/` beside the runtime unless
// TREE_CALCULUS_CACHE names another directory, or is empty to turn it off.
// Beside the runtime, so whoever uses this copy shares one cache — which is
// sound because every entry is content-addressed — and a checkout's cache is
// found, and cleared, with the checkout rather than in each project using it.
// Inside it, each evaluator keeps a directory of its own (see which.mts).
//
// Reduction is pure, so anything derived from a term can be kept forever,
// keyed by what decides it. Three stores live here:
//
//   reduce/  the reduced DAG text of a term, keyed by the term's fingerprint —
//            what lets an `eval` of a symbol skip reduction entirely
//            when nothing it depends on changed.
//   module/  the fully evaluated form of a whole module, keyed by a hash of
//            its text — what lets a module be re-loaded without re-reducing
//            every binding (see `dump` in the native runner).
//   transform/  a transformer's output, keyed by a hash of the program and the
//            input — what lets a rebuild compile only the sources that
//            changed (see transform.mts).
//
// Writes go through a temporary file and a rename, so a concurrent reader —
// or a parallel warmer filling the same cache — never sees half an entry.

import { createHash } from "crypto";
import {
  existsSync, mkdirSync, readFileSync, readdirSync, renameSync, rmSync, statSync,
  utimesSync, writeFileSync,
} from "fs";
import { join } from "path";
import { writer } from "../common.mjs";
import { evaluator_name } from "../runner/which.mjs";

/** The cache every evaluator's directory sits in, or undefined when caching is off. */
export const cache_root = (): string | undefined => {
  const configured = process.env.TREE_CALCULUS_CACHE;
  if (configured === undefined) return join(__dirname, '.cache');
  return configured || undefined;
};

// Store names carry the format version of what is in them: bump one when its
// entries' meaning or encoding changes, and old entries are simply never found
// again.
export const REDUCE_STORE = 'reduce-v1';
export const MODULE_STORE = 'module-v1';
export const TRANSFORM_STORE = 'transform-v1';

// Using an entry stamps it, so its age is how long nothing has needed it. An
// evaluated module goes soonest: there is one per version of a library, each
// large, and only the latest few are ever asked for again.
const DAY = 24 * 60 * 60 * 1000;
const max_age = (name: string): number => (name === MODULE_STORE ? 3 : 30) * DAY;

const subdirectories = (directory: string): string[] => {
  try { return readdirSync(directory, { withFileTypes: true }).filter(e => e.isDirectory()).map(e => e.name); }
  catch { return []; }
};

/**
 * Drop what nothing has used lately, at most once a day per cache: every
 * evaluator's, not only this process's, or one nobody runs any more would
 * stay forever.
 */
function prune(root: string): void {
  const stamp = join(root, '.pruned');
  const now = Date.now();
  try { if (now - statSync(stamp).mtimeMs < DAY) return; } catch { /* never pruned */ }
  mkdirSync(root, { recursive: true });
  writeFileSync(stamp, '');
  for (const evaluator of subdirectories(root)) {
    for (const name of subdirectories(join(root, evaluator))) {
      const directory = join(root, evaluator, name);
      for (const entry of readdirSync(directory)) {
        const at = join(directory, entry);
        try {
          if (now - statSync(at).mtimeMs > max_age(name)) rmSync(at, { force: true });
        } catch { /* gone already, or another pruner's */ }
      }
    }
  }
}

const pruned = new Set<string>();

export const text_key = (text: string): Buffer =>
  createHash('sha256').update(text).digest();

export interface Store {
  path(key: Buffer): string;
  has(key: Buffer): boolean;
  get(key: Buffer): Buffer | null;
  put(key: Buffer, data: Buffer | string): string;
}

/** The store under `<cache_root()>/<evaluator>/<name>`, or null when caching is off. */
export function store(name: string): Store | null {
  const root = cache_root();
  if (root === undefined) return null;
  if (!pruned.has(root)) {
    pruned.add(root);
    prune(root);
  }
  const directory = join(root, evaluator_name(), name);
  mkdirSync(directory, { recursive: true });
  const path = (key: Buffer) => join(directory, key.toString('hex'));
  const touch = (at: string) => {
    try { utimesSync(at, new Date(), new Date()); } catch { /* read-only cache is fine */ }
  };
  return {
    path,
    has: (key) => {
      const at = path(key);
      if (!existsSync(at)) return false;
      touch(at);
      return true;
    },
    get: (key) => {
      const at = path(key);
      if (!existsSync(at)) return null;
      touch(at);
      return readFileSync(at);
    },
    put: (key, data) => {
      const at = path(key);
      // Unique per writer, so nobody else writes the same temporary; the
      // rename is atomic, so whoever lands last wins whole.
      const temporary = `${at}.${writer}.tmp`;
      try {
        writeFileSync(temporary, data);
        renameSync(temporary, at);
      } catch (error) {
        rmSync(temporary, { force: true });
        throw error;
      }
      return at;
    },
  };
}
