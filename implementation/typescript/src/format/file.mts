// Files as trees.
//
// A program that produces a *file* has to say more than what the bytes are: it
// has to say what the file is called and how to interpret it. The convention is
// a fork pairing that metadata with the content,
//
//   △ (△ <name> <media type>) <bytes>
//
// where name and media type are strings and bytes is a buffer. It composes with
// the list convention, so a program returning several files returns a list of
// these.
//
// Recognizing one is necessarily a guess. Ordinary data can have the same shape
// — a string is a fork whose left child is a fork too — so `to_file` insists
// that both halves of the metadata marshal cleanly as text and that the name
// actually looks like a filename before it will claim to have found a file.

import { Evaluator, marshal } from "../common.mjs";

export interface TreeFile {
  name: string;
  media_type: string;
  bytes: Uint8Array;
}

// Deliberately narrow: a leading alphanumeric, an extension, and nothing that
// could escape a directory or upset a filesystem.
const PLAUSIBLE_NAME = /^[a-zA-Z0-9][a-zA-Z0-9._-]*\.[a-zA-Z0-9]+$/;
const MAX_NAME_LENGTH = 255;

export function is_plausible_file_name(name: string): boolean {
  return name.length <= MAX_NAME_LENGTH && PLAUSIBLE_NAME.test(name);
}

export function of_file<TTree>(e: Evaluator<TTree>, file: TreeFile): TTree {
  const m = marshal(e);
  return e.fork(
    e.fork(m.of_string(file.name), m.of_string(file.media_type)),
    m.of_buffer(file.bytes));
}

/** The file `x` represents, or null if it does not look like one. */
export function to_file<TTree>(e: Evaluator<TTree>, x: TTree): TreeFile | null {
  const m = marshal(e);
  const as_fork = e.triage<[TTree, TTree] | null>(() => null, () => null, (u, v) => [u, v]);

  const outer = as_fork(x);
  if (!outer) return null;
  const metadata = as_fork(outer[0]);
  if (!metadata) return null;

  try {
    const name = m.to_string(metadata[0]);
    if (!is_plausible_file_name(name)) return null;
    return { name, media_type: m.to_string(metadata[1]), bytes: m.to_buffer(outer[1]) };
  } catch {
    return null;
  }
}
