// Command line tool for working with DAG modules — DAGs that name their parts
// and can reference each other. See ../../../conventions/ for the conventions
// these operations rely on, and ../../../bin/README.md for usage.

import { readFileSync } from "fs";
import { raise } from "./common.mjs";
import { evaluator as e, formatters } from "./format/formats.mjs";
import { DagModule, is_label } from "./module/module.mjs";
import { environment } from "./module/env.mjs";
import { link, interface_of } from "./module/link.mjs";

// Library exports — the pieces a language's own build tool needs in order to
// drive this in process rather than through the command line.
export { DagModule, box, is_label, is_symbol_name, is_private, LEAF } from "./module/module.mjs";
export { environment } from "./module/env.mjs";
export {
  link, order, interface_of, topological_sort,
  DuplicateExportError, DependencyCycleError,
} from "./module/link.mjs";
export { transformer } from "./module/transform.mjs";
export { to_file, of_file, is_plausible_file_name } from "./format/file.mjs";
export { evaluator, formatters, m as marshal } from "./format/formats.mjs";

const USAGE = `Usage: dag <command> [options] [file...]

Commands:
  link <file>...          Concatenate modules in dependency order. Rejects
                          duplicate exports and dependency cycles.
  canonicalize [file]     Hash-cons into globally unique numeric ids.
  qualify --prefix <p> [file]
                          Namespace a module's exports under <p>. Definitions
                          that are not exported are made unique but stay private.
  extract --symbol <s> [file]
                          Keep only what <s> is built from, as a DAG naming it.
  eval [file]             Evaluate a module and print one of its symbols.
  interface [file]        List what a module exports and what it needs.

Options:
  --prefix <p>            Namespace prefix for 'qualify', e.g. 'Bool.'
  --reserved <regex>      Names 'qualify' must leave alone, on top of labels.
  --symbol <s>            Which symbol 'extract' keeps, or 'eval' prints.
                          'eval' defaults to the last one.
  --format <f>            Output format for 'eval': ${Object.keys(formatters).join(', ')}.
                          Defaults to term.

A file argument of '-', or no file at all, reads stdin.`;

interface Options {
  prefix?: string;
  reserved?: string;
  symbol?: string;
  format: string;
}

const COMMANDS = ['link', 'canonicalize', 'qualify', 'extract', 'eval', 'interface'];

function parse_args(argv: string[]): { command: string, files: string[], options: Options } {
  const command = argv[0];
  if (!COMMANDS.includes(command)) raise(`expected one of ${COMMANDS.join(', ')}, got ${command}`);

  const files: string[] = [];
  const options: Options = { format: 'term' };
  for (let i = 1; i < argv.length; i++) {
    const arg = argv[i];
    const value = () => i + 1 < argv.length ? argv[++i] : raise(`${arg} needs a value`);
    if (arg === '--prefix') options.prefix = value();
    else if (arg === '--reserved') options.reserved = value();
    else if (arg === '--symbol') options.symbol = value();
    else if (arg === '--format') options.format = value();
    else if (arg.startsWith('--')) raise(`unrecognized option ${arg}`);
    else files.push(arg);
  }
  return { command, files, options };
}

const read = (file: string) => readFileSync(file === '-' ? 0 : file, 'utf8');
const read_input = (files: string[]) => read(files.length ? files[0] : '-');

/** What `eval` prints when no symbol is named: whatever the module ends on. */
function last_symbol(text: string): string {
  let last: string | null = null;
  for (const raw of text.split(/\r?\n/)) {
    const words = raw.trim().split(/\s+/).filter(Boolean);
    if (words.length) last = words[0];
  }
  return last ?? raise('module is empty; nothing to evaluate');
}

function run(command: string, files: string[], options: Options): Uint8Array {
  const utf8 = (s: string) => new TextEncoder().encode(s);

  switch (command) {
    case 'link':
      if (!files.length) raise('link needs at least one file');
      return utf8(link(files.map(name => ({ name, text: read(name) }))));

    case 'canonicalize':
      return utf8(DagModule.parse(read_input(files)).canonicalize().toString());

    case 'qualify': {
      const prefix = options.prefix ?? raise('qualify needs --prefix');
      const extra = options.reserved === undefined ? null : new RegExp(options.reserved);
      return utf8(DagModule
        .parse(read_input(files), { absorb_internal_aliases: false })
        .qualify(prefix, { reserved: name => is_label(name) || !!extra?.test(name) })
        .toString());
    }

    case 'extract': {
      const symbol = options.symbol ?? raise('extract needs --symbol');
      return utf8(DagModule.parse(read_input(files)).extract(symbol).toString([symbol]));
    }

    case 'eval': {
      const text = read_input(files);
      const origin = files.length && files[0] !== '-' ? files[0] : '';
      const format = formatters[options.format] ?? raise(`unrecognized format ${options.format}`);
      const value = environment(e, text, { origin })(options.symbol ?? last_symbol(text));
      const out = format.to(value);
      return options.format === 'buffer' ? out : new Uint8Array([...out, 10]);
    }

    case 'interface': {
      const { exports, imports } = interface_of(read_input(files));
      return utf8([
        ...exports.map(s => `export ${s}`),
        ...imports.map(s => `import ${s}`),
      ].join('\n') + '\n');
    }

    default:
      return raise(`unrecognized command ${command}`);
  }
}

if (typeof require !== 'undefined' && require.main === module) {
  const argv = process.argv.slice(2);
  if (!argv.length || argv[0] === '-h' || argv[0] === '--help') {
    console.log(USAGE);
  } else {
    // Usage belongs with a misuse of the command line, not with a module that
    // turned out not to link.
    let parsed;
    try {
      parsed = parse_args(argv);
    } catch (error: any) {
      console.error(`dag: ${error?.message ?? error}\n\n${USAGE}`);
      process.exit(1);
    }
    try {
      process.stdout.write(run(parsed.command, parsed.files, parsed.options));
    } catch (error: any) {
      console.error(`dag ${parsed.command}: ${error?.message ?? error}`);
      process.exit(1);
    }
  }
}
