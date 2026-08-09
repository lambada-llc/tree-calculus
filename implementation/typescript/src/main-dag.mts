// Command line tool for working with DAG modules — DAGs that name their parts
// and can reference each other. See ../../../conventions/ for the conventions
// these operations rely on, and ../../../bin/README.md for usage.

import { readFileSync } from "fs";
import { raise } from "./common.mjs";
import { evaluator as e, formatters } from "./format/formats.mjs";
import { DagModule, is_label, is_symbol_name } from "./module/module.mjs";
import { environment as environment_js } from "./module/env.mjs";
import { link, interface_of } from "./module/link.mjs";
import { transformer as transformer_js } from "./module/transform.mjs";
import { native } from "./runner/native.mjs";

// Reduction happens either in Node or in the C++ runner, and the choice is made
// here, once: everything downstream — this file's own `eval`, and every build
// tool that imports the two below — keeps the same signatures either way. See
// ./runner/native.mts for what opts in and why it is not the default.
export const environment = native?.environment ?? environment_js;
export const transformer = native?.transformer ?? transformer_js;

// Library exports — the pieces a language's own build tool needs in order to
// drive this in process rather than through the command line.
export { DagModule, box, is_label, is_symbol_name, is_private, LEAF } from "./module/module.mjs";
export {
  link, order, interface_of, topological_sort,
  DuplicateExportError, DependencyCycleError,
} from "./module/link.mjs";
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
  extract --symbol <s>... [file]
                          Keep only what the named symbols are built from, as a
                          DAG naming them. Several is not the same as several
                          extracts: what two of them share is kept once.
  eval [file]             Evaluate a module and print one of its symbols.
  interface [file]        List what a module exports and what it needs.

Options:
  --prefix <p>            Namespace prefix for 'qualify', e.g. 'Bool.'
  --reserved <regex>      Names 'qualify' must leave alone, on top of labels.
  --symbol <s>            Which symbol 'extract' keeps — repeat it for several —
                          or which one 'eval' prints. 'eval' defaults to the
                          last one.
  --except <regex>        Name 'extract's symbols by what they are not: every
                          symbol the module defines but those matching. How a
                          library is stripped of its tests, which are the only
                          thing nothing else is built from.
  --format <f>            Output format for 'eval': ${Object.keys(formatters).join(', ')}.
                          Defaults to term.

A file argument of '-', or no file at all, reads stdin.`;

interface Options {
  prefix?: string;
  reserved?: string;
  symbols: string[];
  except?: string;
  format: string;
}

const COMMANDS = ['link', 'canonicalize', 'qualify', 'extract', 'eval', 'interface'];

function parse_args(argv: string[]): { command: string, files: string[], options: Options } {
  const command = argv[0];
  if (!COMMANDS.includes(command)) raise(`expected one of ${COMMANDS.join(', ')}, got ${command}`);

  const files: string[] = [];
  const options: Options = { symbols: [], format: 'term' };
  for (let i = 1; i < argv.length; i++) {
    const arg = argv[i];
    const value = () => i + 1 < argv.length ? argv[++i] : raise(`${arg} needs a value`);
    if (arg === '--prefix') options.prefix = value();
    else if (arg === '--reserved') options.reserved = value();
    else if (arg === '--symbol') options.symbols.push(value());
    else if (arg === '--except') options.except = value();
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
      const module = DagModule.parse(read_input(files));
      // Named definitions are what a reader can ask for; an id is scaffolding
      // reachable only through the lines that use it, so it is never a root of
      // its own. The same notion of an interface `partition` works from.
      const excluded = options.except === undefined ? null : new RegExp(options.except);
      const matched = excluded === null ? [] : [...new Set(module.lines
        .filter(line => line.length > 1 && is_symbol_name(line[0].symbol))
        .map(line => line[0].symbol)
        .filter(name => !excluded.test(name)))];
      const symbols = [...new Set([...options.symbols, ...matched])];
      if (!symbols.length) raise('extract needs --symbol or --except');
      // A DAG ends in the name of its value, and a set of them has no single
      // one to end on — so the entry line is written only when one was asked
      // for, and several produce definitions to be read against, as a library.
      return utf8(module.extract(...symbols).toString(symbols.length === 1 ? symbols : []));
    }

    case 'eval': {
      const text = read_input(files);
      const origin = files.length && files[0] !== '-' ? files[0] : '';
      const format = formatters[options.format] ?? raise(`unrecognized format ${options.format}`);
      const value = environment(e, text, { origin })(options.symbols.at(-1) ?? last_symbol(text));
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
