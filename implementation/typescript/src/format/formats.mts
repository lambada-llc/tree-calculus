// The named formats the command line tools speak, in one table.
//
// Each entry converts between raw bytes and a tree: the data conventions
// (`bool`, `nat`, `string`, `buffer`) via marshalling, the tree conventions
// (`ternary`, `term`, `dag`, `minbin`) via formatters.

import { Evaluator, marshal, raise } from "../common.mjs";
import e from "../evaluator/lazy-stacks.mjs";
import formatter_dag from "./dag.mjs";
import formatter_ternary from "./ternary.mjs";
import formatter_readable from "./readable.mjs";
import formatter_minbin from "./minbin.mjs";
import { Formatter } from "./formatter.mjs";

export type TTree = typeof e extends Evaluator<infer T> ? T : never;

export const text_enc = new TextEncoder();
export const text_dec = new TextDecoder();
export const m = marshal(e);

const of_marshaller = <T,>(
  of: (x: T) => TTree,
  to: (x: TTree) => T,
  of_string: (s: string) => T,
  to_string: (x: T) => string) => ({
    of: (s: Uint8Array) => of(of_string(text_dec.decode(s))),
    to: (x: TTree) => text_enc.encode(to_string(to(x)))
  });
const of_formatter = (f: Formatter) => ({
  of: (s: Uint8Array) => f.of(e, text_dec.decode(s)),
  to: (x: TTree) => text_enc.encode(f.to(e, x))
});

export const formatters: { [format: string]: { of: (s: Uint8Array) => TTree, to: (x: TTree) => Uint8Array } } = {
  bool: of_marshaller(
    m.of_bool,
    m.to_bool,
    s => s === 'true' ? true : s === 'false' ? false : raise('invalid boolean'),
    x => x ? 'true' : 'false'
  ),
  nat: of_marshaller(
    m.of_nat,
    m.to_nat,
    s => BigInt(s),
    x => x.toString()
  ),
  string: of_marshaller(
    m.of_string,
    m.to_string,
    s => s,
    x => x
  ),
  buffer: {
    of: (s: Uint8Array) => m.of_buffer(s),
    to: (x: TTree) => m.to_buffer(x),
  },
  ternary: of_formatter(formatter_ternary),
  dag: of_formatter(formatter_dag),
  term: of_formatter(formatter_readable),
  minbin: of_formatter(formatter_minbin),
};

export const parse_infer = (s: Uint8Array): [TTree, string] => {
  const guess = (format: string): [TTree, string] | null => {
    const f = formatters[format];
    try {
      return [f.of(s), format];
    } catch {
      return null;
    }
  };
  return guess('bool')
    || guess('ternary')
    || guess('nat')
    || guess('term')
    || guess('dag')
    || guess('string')
    || guess('buffer')
    || raise(`could not infer format (unexpected, [buffer] should always work)`);
};

export type Parser_infer = (s: Uint8Array) => [TTree, string];
export const formatters_infer: { [format: string]: Parser_infer } = {};
for (const format in formatters)
  formatters_infer[format] = (s: Uint8Array) => [formatters[format].of(s), format];
formatters_infer['infer'] = parse_infer;

export { e as evaluator };
