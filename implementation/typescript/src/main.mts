import { id, raise } from "./common.mjs";
import formatter_dag from "./format/dag.mjs";
import formatter_ternary from "./format/ternary.mjs";
import formatter_readable from "./format/readable.mjs";
import formatter_minbin from "./format/minbin.mjs";
import {
  evaluator as e, formatters, formatters_infer, m, text_dec, text_enc, TTree,
} from "./format/formats.mjs";
import { readFileSync } from "fs";

// Library exports
export { e as evaluator, formatters, m as marshal, formatter_dag, formatter_ternary, formatter_readable, formatter_minbin, id };
export { children } from "./common.mjs";

// CLI
if (typeof require !== 'undefined' && require.main === module) {
  const args = process.argv.slice(2);
  let input_mode_file = false;
  let current_format = 'infer';
  let last_format = 'term';
  let current_value: TTree = id(e);
  for (const raw_arg of args) {
    if (raw_arg.startsWith('-') && raw_arg.length > 1) {
      const arg = raw_arg.replace(/^-+/, '');
      if (arg === 'file') input_mode_file = true;
      else if (arg in formatters_infer) last_format = current_format = arg;
      else raise(`unrecognized format ${arg}`);
    }
    else {
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
}
