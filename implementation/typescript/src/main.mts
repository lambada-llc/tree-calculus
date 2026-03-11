import { Evaluator, id, marshal, raise } from "./common.mjs";
import e from "./evaluator/lazy-stacks.mjs";
import formatter_dag from "./format/dag.mjs";
import formatter_ternary from "./format/ternary.mjs";
import formatter_readable from "./format/readable.mjs";
import formatter_minbin from "./format/minbin.mjs";
import { Formatter } from "./format/formatter.mjs";
import { readFileSync } from "fs";
import { buffer } from "stream/consumers";

type TTree = typeof e extends Evaluator<infer TTree> ? TTree : never;

// Formatters
const text_enc = new TextEncoder();
const text_dec = new TextDecoder();
const m = marshal(e);
const of_marshaller = <T,>(
  of: (x: T) => TTree,
  to: (x: TTree) => T,
  of_string: (s: string) => T,
  to_string: (x: T) => string) => ({
    of: (s: Uint8Array) => of(of_string(text_dec.decode(s))),
    to: (x: TTree) => text_enc.encode(to_string(to(x)))
  });
const of_formatter = (f: Formatter) => ({
  of: (s: Uint8Array) => f.of(e,  text_dec.decode(s)),
  to: (x: TTree) => text_enc.encode(f.to(e, x))
});
const formatters: { [format: string]: { of: (s: Uint8Array) => TTree, to: (x: TTree) => Uint8Array } } = {
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
const parse_infer = (s: Uint8Array): [TTree, string] => {
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
type Parser_infer = (s: Uint8Array) => [TTree, string];
const formatters_infer: { [format: string]: Parser_infer } = {};
for (const format in formatters)
  formatters_infer[format] = (s: Uint8Array) => [formatters[format].of(s), format];
formatters_infer['infer'] = parse_infer;

// Process arguments
const args = process.argv.slice(2);
let input_mode_file = false;
let current_format = 'infer';
let last_format = 'term';
let current_value: TTree = id(e);
for (const raw_arg of args) {
  if (raw_arg.startsWith('-') && raw_arg.length > 1) {
    // set format
    const arg = raw_arg.replace(/^-+/, '');
    if (arg === 'file') input_mode_file = true;
    else if (arg in formatters_infer) last_format = current_format = arg;
    else raise(`unrecognized format ${arg}`);
  }
  else {
    // parse file
    const content =
      raw_arg === '-'
      ? new Uint8Array(readFileSync(0))
      : input_mode_file
        ? new Uint8Array(readFileSync(raw_arg))
        : text_enc.encode(raw_arg);
    input_mode_file = false;
    const [value, format] = formatters_infer[current_format](content);
    last_format = format;
    current_value = e.apply(current_value, value);
  }
}

if (last_format == 'buffer')
  process.stdout.write(formatters[last_format].to(current_value));
else
  console.log(text_dec.decode(formatters[last_format].to(current_value)));

