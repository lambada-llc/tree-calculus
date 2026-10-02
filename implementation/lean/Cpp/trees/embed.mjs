#!/usr/bin/env node
// trees/*.dag → ../Jets/Trees.lean (through ../dag2lean.mjs) and implementation/cpp/jets.hpp,
// which holds each DAG verbatim for the runtime to intern: the trees the theorems are about.
//
//   node implementation/lean/Cpp/trees/embed.mjs [--check: fail if either is not what it writes]

import { execFileSync } from 'node:child_process';
import { readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const here = import.meta.dirname;
// Sorted: the order numbers dag2lean.mjs's shared definitions.
const trees = readdirSync(here).filter((f) => f.endsWith('.dag')).sort()
  .map((f) => [f.slice(0, -'.dag'.length), join(here, f)]);
const outputs = [
  [join(here, '../Jets/Trees.lean'), execFileSync('node',
    [join(here, '../dag2lean.mjs'), 'Cpp.Jets', ...trees.map(([t, f]) => `${t}=${f}`)]).toString()],
  [join(here, '../../../cpp/jets.hpp'), `// Generated from implementation/lean/Cpp/trees/*.dag by embed.mjs there; do not edit.
#pragma once

namespace jets {
${trees.map(([t, f]) => `inline constexpr char ${t}[] = R"dag(${readFileSync(f, 'utf8')})dag";`).join('\n')}
} // namespace jets
`],
];
for (const [path, text] of outputs)
  if (process.argv[2] !== '--check') writeFileSync(path, text);
  else if (readFileSync(path, 'utf8') !== text) throw new Error(`${path} is stale: run ${process.argv[1]}`);
