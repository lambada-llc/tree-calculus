// The on-disk reduction cache, opted into with TREE_CALCULUS_CACHE=<dir>.
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
  existsSync, mkdirSync, readFileSync, renameSync, rmSync, utimesSync, writeFileSync,
} from "fs";
import { join } from "path";
import { writer } from "../common.mjs";

export const cache_dir = (): string | undefined =>
  process.env.TREE_CALCULUS_CACHE || undefined;

// Store names carry the format version of what is in them: bump one when its
// entries' meaning or encoding changes, and old entries are simply never found
// again. Exported because a cache warmer needs to probe the same stores the
// runtime will read.
export const REDUCE_STORE = 'reduce-v1';
export const MODULE_STORE = 'module-v1';
export const TRANSFORM_STORE = 'transform-v1';

export const text_key = (text: string): Buffer =>
  createHash('sha256').update(text).digest();

export interface Store {
  path(key: Buffer): string;
  has(key: Buffer): boolean;
  get(key: Buffer): Buffer | null;
  put(key: Buffer, data: Buffer | string): string;
}

/** The store under `<TREE_CALCULUS_CACHE>/<name>`, or null when caching is off. */
export function store(name: string): Store | null {
  const base = cache_dir();
  if (base === undefined) return null;
  const directory = join(base, name);
  mkdirSync(directory, { recursive: true });
  const path = (key: Buffer) => join(directory, key.toString('hex'));
  // Using an entry stamps it, so mtime means "last needed" rather than "first
  // written" and a pruner keeping recent entries is keeping the used ones.
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
