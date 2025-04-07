import { Evaluator, id, marshal, raise } from "./common.mjs";
import e from "./evaluator/eager-stacks.mjs";
import formatter_dag from "./format/dag.mjs";
import formatter_ternary from "./format/ternary.mjs";
import formatter_readable from "./format/readable.mjs";
import { Formatter } from "./format/formatter.mjs";

type TTree = typeof e extends Evaluator<infer TTree> ? TTree : never;

// Formatters
const m = marshal(e);
const of_marshaller = <T,>(
  of: (x: T) => TTree,
  to: (x: TTree) => T,
  of_string: (s: string) => T,
  to_string: (x: T) => string) => ({
    of: (s: string) => of(of_string(s)),
    to: (x: TTree) => to_string(to(x))
  });
const of_formatter = (f: Formatter) => ({
  of: (s: string) => f.of(e, s),
  to: (x: TTree) => f.to(e, x)
});
const formatters: { [format: string]: { of: (s: string) => TTree, to: (x: TTree) => string } } = {
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
  ternary: of_formatter(formatter_ternary),
  dag: of_formatter(formatter_dag),
  term: of_formatter(formatter_readable),
};
const parse_infer = (s: string): [TTree, string] => {
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
    || raise(`could not infer format`);
};
type Parser_infer = (s: string) => [TTree, string];
const formatters_infer: { [format: string]: Parser_infer } = {};
for (const format in formatters)
  formatters_infer[format] = (s: string) => [formatters[format].of(s), format];
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
        ? require('fs').readFileSync(0, 'utf8').trimEnd()
        : input_mode_file
          ? require('fs').readFileSync(raw_arg, 'utf8').trimEnd()
          : raw_arg;
    input_mode_file = false;
    const [value, format] = formatters_infer[current_format](content);
    last_format = format;
    current_value = e.apply(current_value, value);
  }
}

console.log(formatters[last_format].to(current_value));

