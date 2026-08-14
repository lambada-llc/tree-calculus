// Content addresses for the terms a DAG names.
//
// A term's fingerprint is a Merkle hash of its application structure: the leaf
// is a constant, and an application mixes the fingerprints of its two sides.
// Equal fingerprints mean equal terms, whatever the lines, aliases and ids
// around them happen to look like — which is what lets the result of a
// reduction be cached across builds. Canonicalization renumbers every id in a
// bundle the moment anything changes, but a term that did not itself change
// keeps its fingerprint, so its cache entry stays found.
//
// Fingerprints identify unreduced structure. Reduction is deterministic, so
// "same term" is exactly the license to reuse "same result" — nothing about
// evaluation order or evaluator is part of the address.

import { createHash } from "crypto";
import { LEAF } from "./module.mjs";

const mix = (parts: Buffer[]): Buffer => {
  const h = createHash('sha256');
  for (const part of parts) h.update(part);
  return h.digest();
};

const LEAF_FINGERPRINT = mix([Buffer.from('tree-calculus:leaf')]);

export interface Fingerprinted {
  /** Fingerprint of every name the text defines (its last definition). */
  fingerprints: Map<string, Buffer>;
  /** Fingerprint of the value the text ends on, if it ends on one. */
  value?: Buffer;
}

/**
 * Fingerprint every line of DAG text.
 *
 * `outer` resolves names the text itself does not define — the loaded module
 * behind an expression, exactly as reduction scopes it. An unresolvable name
 * throws: a caller using fingerprints as cache keys must not key on a term it
 * could not actually see all of.
 */
export function fingerprint(
  text: string,
  outer?: (name: string) => Buffer | undefined,
): Fingerprinted {
  const fingerprints = new Map<string, Buffer>();
  const resolve = (name: string): Buffer => {
    if (name === LEAF) return LEAF_FINGERPRINT;
    const own = fingerprints.get(name) ?? outer?.(name);
    if (own === undefined) throw new Error(`unbound symbol: ${name}`);
    return own;
  };

  let value: Buffer | undefined;
  for (const line of text.split(/\r?\n/)) {
    const words = line.split(' ').filter(Boolean);
    if (words.length === 3) fingerprints.set(words[0], mix([resolve(words[1]), resolve(words[2])]));
    else if (words.length === 2) fingerprints.set(words[0], resolve(words[1]));
    else if (words.length === 1) value = resolve(words[0]);
  }
  return { fingerprints, value };
}
