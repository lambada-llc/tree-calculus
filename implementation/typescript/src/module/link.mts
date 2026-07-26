// Linking DAG modules.
//
// Modules reference each other by name, with no import statements: what a module
// needs is simply what it mentions but does not define. Linking works out the
// resulting dependency order and concatenates the modules in it, so that every
// reference is defined above its use and the result parses as one DAG.
//
// Two things make a set of modules unlinkable, and both are reported rather than
// silently resolved: two modules exporting the same name (which of them did a
// reference mean?) and a dependency cycle (no order satisfies everyone).

import { raise } from "../common.mjs";

export interface Fragment {
  /** How this module is identified in errors, typically a file path. */
  name: string;
  text: string;
}

export interface Interface {
  /** Names other modules may reference. */
  exports: string[];
  /** Names mentioned but not defined here. */
  imports: string[];
}

export interface Edge {
  /** Must be linked before `to`. */
  from: string;
  to: string;
}

export class DuplicateExportError extends Error {
  constructor(public duplicates: { symbol: string, sources: string[] }[]) {
    super('duplicate exports:\n' + duplicates
      .map(d => `  ${d.symbol}: ${d.sources.join(' ')}`).join('\n'));
    this.name = 'DuplicateExportError';
  }
}

export class DependencyCycleError extends Error {
  constructor(public cycle: string[], public edges: { symbol: string, from: string, to: string }[]) {
    super('dependency cycle:\n  ' + [...cycle, cycle[0]].join(' → ')
      + (edges.length ? '\nedges:\n' + edges
        .map(e => `  ${e.to} imports ${e.symbol} from ${e.from}`).join('\n') : ''));
    this.name = 'DependencyCycleError';
  }
}

/**
 * What a module offers and what it needs.
 *
 * Exports are two-word definitions — a name bound to an existing value, which is
 * how a module publishes something. Three-word lines build intermediate nodes
 * and are not an interface. Names containing ':' are module-internal by
 * convention (labels like `:i`, and whatever `qualify` marked as non-exported),
 * so they are never offered.
 *
 * Imports are read positionally rather than resolved: any word after the first
 * that has not been defined further up must come from elsewhere.
 */
export function interface_of(text: string): Interface {
  const defined = new Set<string>();
  const exports = new Set<string>();
  const imports = new Set<string>();

  for (const raw of text.split('\n')) {
    const line = raw.trim();
    if (!line || line.startsWith('#')) continue;
    const words = line.split(/\s+/);
    for (let i = 1; i < words.length; i++)
      if (!defined.has(words[i])) imports.add(words[i]);
    defined.add(words[0]);
    if (words.length === 2 && !words[0].includes(':')) exports.add(words[0]);
  }

  return { exports: [...exports], imports: [...imports] };
}

/**
 * Order `items` so that every edge points forwards.
 *
 * Ties are broken alphabetically so that the same inputs always link to the same
 * output — the point of the whole pipeline is that a rebuild that changed
 * nothing produces bytes that changed nothing.
 */
export function topological_sort<T>(items: T[], edges: Edge[], key: (item: T) => string): T[] {
  const by_key = new Map<string, T>();
  for (const item of items) by_key.set(key(item), item);
  const keys = [...by_key.keys()].sort();

  const indegree = new Map<string, number>();
  const successors = new Map<string, string[]>();
  for (const k of keys) { indegree.set(k, 0); successors.set(k, []); }
  for (const { from, to } of edges) {
    if (!indegree.has(to) || !successors.has(from)) continue;
    indegree.set(to, indegree.get(to)! + 1);
    successors.get(from)!.push(to);
  }

  const done = new Set<string>();
  const result: T[] = [];
  while (result.length < keys.length) {
    const ready = keys.find(k => !done.has(k) && indegree.get(k) === 0);
    if (ready === undefined) return raise_cycle(keys.filter(k => !done.has(k)), edges);
    result.push(by_key.get(ready)!);
    done.add(ready);
    for (const next of successors.get(ready)!)
      indegree.set(next, indegree.get(next)! - 1);
  }
  return result;
}

/**
 * Every remaining node has an unmet dependency, so following edges from any of
 * them has to revisit one — and the second visit closes an actual cycle, which
 * is far more useful to report than the set of nodes that got stuck.
 */
function raise_cycle(remaining: string[], edges: Edge[]): never {
  const left = new Set(remaining);
  const successors = new Map<string, string[]>();
  for (const k of left) successors.set(k, []);
  for (const { from, to } of edges)
    if (left.has(from) && left.has(to)) successors.get(from)!.push(to);

  const seen = new Map<string, number>();
  const path: string[] = [];
  let current = remaining[0];
  for (; ;) {
    if (seen.has(current)) throw { cycle: path.slice(seen.get(current)!) };
    seen.set(current, path.length);
    path.push(current);
    const next = successors.get(current)?.[0];
    if (next === undefined) raise('no cycle found among stuck modules (unexpected)');
    current = next!;
  }
}

/** Dependency order for `fragments`, dependencies first. */
export function order(fragments: Fragment[]): Fragment[] {
  const interfaces = new Map(fragments.map(f => [f.name, interface_of(f.text)]));

  const exporters = new Map<string, string[]>();
  for (const f of fragments)
    for (const symbol of interfaces.get(f.name)!.exports)
      exporters.set(symbol, [...(exporters.get(symbol) ?? []), f.name]);

  const duplicates = [...exporters]
    .filter(([, sources]) => sources.length > 1)
    .map(([symbol, sources]) => ({ symbol, sources }));
  if (duplicates.length) throw new DuplicateExportError(duplicates);

  const dependencies: { symbol: string, from: string, to: string }[] = [];
  for (const f of fragments)
    for (const symbol of interfaces.get(f.name)!.imports) {
      const from = exporters.get(symbol)?.[0];
      if (from !== undefined && from !== f.name)
        dependencies.push({ symbol, from, to: f.name });
    }

  try {
    return topological_sort(fragments, dependencies, f => f.name);
  } catch (e: any) {
    if (!e?.cycle) throw e;
    const in_cycle = new Set<string>(e.cycle);
    throw new DependencyCycleError(e.cycle, dependencies
      .filter(d => in_cycle.has(d.from) && in_cycle.has(d.to)));
  }
}

/** Dependency-ordered concatenation of `fragments` into a single DAG. */
export function link(fragments: Fragment[]): string {
  return order(fragments)
    .map(f => f.text.endsWith('\n') ? f.text : f.text + '\n')
    .join('');
}
