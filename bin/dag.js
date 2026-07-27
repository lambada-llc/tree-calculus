#!/usr/bin/env node
"use strict";
var __defProp = Object.defineProperty;
var __getOwnPropDesc = Object.getOwnPropertyDescriptor;
var __getOwnPropNames = Object.getOwnPropertyNames;
var __hasOwnProp = Object.prototype.hasOwnProperty;
var __export = (target, all) => {
  for (var name in all)
    __defProp(target, name, { get: all[name], enumerable: true });
};
var __copyProps = (to5, from, except, desc) => {
  if (from && typeof from === "object" || typeof from === "function") {
    for (let key of __getOwnPropNames(from))
      if (!__hasOwnProp.call(to5, key) && key !== except)
        __defProp(to5, key, { get: () => from[key], enumerable: !(desc = __getOwnPropDesc(from, key)) || desc.enumerable });
  }
  return to5;
};
var __toCommonJS = (mod) => __copyProps(__defProp({}, "__esModule", { value: true }), mod);

// src/main-dag.mjs
var main_dag_exports = {};
__export(main_dag_exports, {
  DagModule: () => DagModule,
  DependencyCycleError: () => DependencyCycleError,
  DuplicateExportError: () => DuplicateExportError,
  LEAF: () => LEAF,
  box: () => box,
  environment: () => environment,
  evaluator: () => lazy_stacks_default,
  formatters: () => formatters,
  interface_of: () => interface_of,
  is_label: () => is_label,
  is_plausible_file_name: () => is_plausible_file_name,
  is_private: () => is_private,
  is_symbol_name: () => is_symbol_name,
  link: () => link,
  marshal: () => m,
  of_file: () => of_file,
  order: () => order,
  to_file: () => to_file,
  topological_sort: () => topological_sort,
  transformer: () => transformer
});
module.exports = __toCommonJS(main_dag_exports);
var import_fs2 = require("fs");

// src/common.mjs
function children(e, x) {
  return e.triage(() => [], (u) => [u], (u, v) => [u, v])(x);
}
var raise = (message) => {
  throw new Error(message);
};
function marshal(e) {
  const t_false = e.leaf;
  const t_true = e.stem(e.leaf);
  const to_bool = e.triage(() => false, (_) => true, (_) => raise("tree is not a bool"));
  const of_bool = (b) => b ? t_true : t_false;
  const to_list = (t) => {
    let l = [];
    const triage = e.triage(() => false, (_) => raise("tree is not a list"), (hd, tl) => (l.push(hd), t = tl, true));
    while (triage(t))
      ;
    return l;
  };
  const of_list = (l) => {
    let f = e.leaf;
    for (let i = l.length; i; i--)
      f = e.fork(l[i - 1], f);
    return f;
  };
  const to_nat = (t) => to_list(t).reduceRight((acc, b) => 2n * acc + (to_bool(b) ? 1n : 0n), 0n);
  const of_nat = (n) => {
    let l = [];
    for (; n; n >>= 1n)
      l.push(of_bool(n % 2n == 1n));
    return of_list(l);
  };
  const to_string = (t) => to_list(t).map(to_nat).map((x) => String.fromCodePoint(Number(x))).join("");
  const of_string = (s) => of_list(Array.from(s).map((c) => of_nat(BigInt(c.codePointAt(0)))));
  const to_buffer = (t) => new Uint8Array(to_list(t).map(to_nat).map((n) => Number(n)));
  const of_buffer = (s) => of_list([...s].map((n) => of_nat(BigInt(n))));
  return {
    to_bool,
    of_bool,
    to_list,
    of_list,
    to_nat,
    of_nat,
    to_string,
    of_string,
    to_buffer,
    of_buffer
  };
}

// src/evaluator/lazy-stacks.mjs
var reduce_one = function* (s) {
  while (s.length >= 3) {
    debug.num_steps++;
    const x = s.pop(), y = s.pop(), z = s.pop();
    if (x.length > 2)
      yield x;
    if (x.length === 0) {
      if (y.length > 2)
        yield y;
      s.push(...y);
    } else if (x.length === 1) {
      if (x[0].length > 2)
        yield x[0];
      s.push([z, ...y], z, ...x[0]);
    } else if (x.length === 2) {
      if (z.length > 2)
        yield z;
      if (z.length === 0) {
        if (x[1].length > 2)
          yield x[1];
        s.push(...x[1]);
      } else if (z.length === 1) {
        if (x[0].length > 2)
          yield x[0];
        s.push(z[0], ...x[0]);
      } else if (z.length === 2) {
        if (y.length > 2)
          yield y;
        s.push(z[0], z[1], ...y);
      }
    }
  }
};
function force_root(expression) {
  const force = [reduce_one(expression)];
  while (force.length > 0) {
    const next = force[force.length - 1].next();
    if (next.done) {
      force.pop();
    } else {
      force.push(reduce_one(next.value));
    }
  }
}
var evaluator = {
  // construct
  leaf: [],
  stem: (u) => [u],
  fork: (u, v) => [v, u],
  // eval
  apply: (a, b) => [b, ...a],
  // destruct
  triage: (on_leaf, on_stem, on_fork) => (x) => {
    force_root(x);
    switch (x.length) {
      case 0:
        return on_leaf();
      case 1:
        return on_stem(x[0]);
      case 2:
        return on_fork(x[1], x[0]);
      default:
        throw new Error("not a value/binary tree");
    }
  }
};
var debug = { num_steps: 0 };
var lazy_stacks_default = evaluator;

// src/format/dag.mjs
function to(e, x) {
  const res = [];
  let i = 0;
  const app_keys = {};
  const apply_keys = (a, b) => {
    const app_key = `${a} ${b}`;
    const alloc = () => {
      const x2 = `${i++}`;
      res.push(`${x2} ${app_key}`);
      return x2;
    };
    return app_keys[app_key] ?? (app_keys[app_key] = alloc());
  };
  const keys = /* @__PURE__ */ new Map();
  const todo = [{ node: x, enter: true }];
  while (todo.length) {
    const { node, enter } = todo.pop();
    if (keys.has(node))
      continue;
    if (enter) {
      todo.push({ node, enter: false });
      for (const c of children(e, node))
        todo.push({ node: c, enter: true });
    } else {
      let current = "\u25B3";
      for (const c of children(e, node))
        current = apply_keys(current, keys.get(c));
      keys.set(node, current);
    }
  }
  res.push(keys.get(x));
  return res.join("\n");
}
function of(e, s) {
  const env = { "\u25B3": e.leaf };
  const get_env = (name) => name in env ? env[name] : raise(`unbound variable: ${name}`);
  for (const line of s.split(/\r?\n/)) {
    const [a, b, c] = line.split(" ");
    if (c)
      env[a] = e.apply(get_env(b), get_env(c));
    else if (b)
      env[a] = get_env(b);
    else if (a)
      return get_env(a);
  }
  return raise("dag representation was unepxectedly not terminated by a value");
}
var formatter = { to, of };
var dag_default = formatter;

// src/format/ternary.mjs
function to2(e, x) {
  const res = [];
  const triage = e.triage(() => res.push("0"), (u) => (res.push("1"), triage(u)), (u, v) => (res.push("2"), triage(u), triage(v)));
  triage(x);
  return res.join("");
}
function of2(e, s) {
  const stack = s.split("").reverse();
  const f = () => {
    const c = stack.pop();
    if (c === void 0)
      raise("unexpected end of ternary encoding");
    switch (c) {
      case "0":
        return e.leaf;
      case "1":
        return e.stem(f());
      case "2":
        return e.fork(f(), f());
      default:
        return raise(`unexpected character in ternary encoding: ${c}`);
    }
  };
  return f();
}
var formatter2 = { to: to2, of: of2 };
var ternary_default = formatter2;

// src/format/readable.mjs
function to3(e, x) {
  const triage = e.triage(() => "\u25B3", (u) => `(\u25B3 ${triage(u)})`, (u, v) => `(\u25B3 ${triage(u)} ${triage(v)})`);
  return e.triage(() => "\u25B3", (u) => `\u25B3 ${triage(u)}`, (u, v) => `\u25B3 ${triage(u)} ${triage(v)}`)(x);
}
function of3(e, s) {
  const id = e.fork(e.stem(e.stem(e.leaf)), e.leaf);
  const stack = [id];
  const apply = (x) => stack[stack.length - 1] = e.apply(stack[stack.length - 1] || raise("unmatched parentheses"), x);
  for (const c of s) {
    switch (c) {
      case "\u25B3":
        apply(e.leaf);
        break;
      case "(":
        stack.push(id);
        break;
      case ")":
        apply(stack.pop() || raise("unmatched parentheses"));
        break;
      case " ":
        break;
      default:
        raise(`unexpected character: ${c}`);
    }
  }
  const res = stack.pop();
  if (res === void 0 || stack.length > 0)
    return raise("unmatched parentheses");
  return res;
}
var formatter3 = { to: to3, of: of3 };
var readable_default = formatter3;

// src/format/minbin.mjs
function to4(e, x) {
  const res = [];
  const triage = e.triage(() => res.push("1"), (u) => (res.push("0"), res.push("1"), triage(u)), (u, v) => (res.push("0"), res.push("0"), res.push("1"), triage(u), triage(v)));
  triage(x);
  return res.join("");
}
function of4(e, s) {
  const stack = s.split("").reverse();
  const f = () => {
    const c = stack.pop();
    if (c === void 0)
      raise("unexpected end of minimalist binary encoding");
    switch (c) {
      case "1":
        return e.leaf;
      case "0": {
        const func = f();
        const arg = f();
        return e.apply(func, arg);
      }
      default:
        return raise(`unexpected character in minimalist binary encoding: ${c}`);
    }
  };
  const result = f();
  if (stack.length > 0)
    raise("trailing characters in minimalist binary encoding");
  return result;
}
var formatter4 = { to: to4, of: of4 };
var minbin_default = formatter4;

// src/format/formats.mjs
var text_enc = new TextEncoder();
var text_dec = new TextDecoder();
var m = marshal(lazy_stacks_default);
var of_marshaller = (of5, to5, of_string, to_string) => ({
  of: (s) => of5(of_string(text_dec.decode(s))),
  to: (x) => text_enc.encode(to_string(to5(x)))
});
var of_formatter = (f) => ({
  of: (s) => f.of(lazy_stacks_default, text_dec.decode(s)),
  to: (x) => text_enc.encode(f.to(lazy_stacks_default, x))
});
var formatters = {
  bool: of_marshaller(m.of_bool, m.to_bool, (s) => s === "true" ? true : s === "false" ? false : raise("invalid boolean"), (x) => x ? "true" : "false"),
  nat: of_marshaller(m.of_nat, m.to_nat, (s) => BigInt(s), (x) => x.toString()),
  string: of_marshaller(m.of_string, m.to_string, (s) => s, (x) => x),
  buffer: {
    of: (s) => m.of_buffer(s),
    to: (x) => m.to_buffer(x)
  },
  ternary: of_formatter(ternary_default),
  dag: of_formatter(dag_default),
  term: of_formatter(readable_default),
  minbin: of_formatter(minbin_default)
};
var parse_infer = (s) => {
  const guess = (format) => {
    const f = formatters[format];
    try {
      return [f.of(s), format];
    } catch {
      return null;
    }
  };
  return guess("bool") || guess("ternary") || guess("nat") || guess("term") || guess("dag") || guess("string") || guess("buffer") || raise(`could not infer format (unexpected, [buffer] should always work)`);
};
var formatters_infer = {};
for (const format in formatters)
  formatters_infer[format] = (s) => [formatters[format].of(s), format];
formatters_infer["infer"] = parse_infer;

// src/module/module.mjs
var LEAF = "\u25B3";
function box(symbol) {
  return { symbol };
}
function is_symbol_name(s) {
  return /^[:a-zA-Z]/.test(s);
}
function is_label(name) {
  return name.startsWith(":");
}
function is_private(name) {
  const local = name.slice(name.lastIndexOf(".") + 1);
  return local !== "_" && local.startsWith("_");
}
var DagModule = class _DagModule {
  constructor() {
    this.lines = [];
  }
  /**
   * Render back to DAG text. `entries` are appended as bare lines, which is how
   * a module is closed into a plain DAG naming one value.
   */
  toString(entries = []) {
    const rendered = this.lines.map((line) => line.map((b) => b.symbol).join(" "));
    rendered.push(...entries);
    return rendered.join("\n") + "\n";
  }
  static parse(text, options = {}) {
    const { absorb_internal_aliases = true } = options;
    const module2 = new _DagModule();
    const latest = /* @__PURE__ */ Object.create(null);
    const resolve2 = (symbol) => latest[symbol] ?? (latest[symbol] = box(symbol));
    for (const raw of text.split("\n")) {
      const trimmed = raw.trim();
      if (!trimmed)
        continue;
      const words = trimmed.split(/\s+/);
      if (absorb_internal_aliases && words.length === 3 && words[1] === ":i") {
        latest[words[0]] = resolve2(words[2]);
        continue;
      }
      const is_definition = words.length === 2 || words.length === 3;
      const head = is_definition ? box(words[0]) : null;
      module2.lines.push(words.map((word, i) => i === 0 && head ? head : resolve2(word)));
      if (head)
        latest[head.symbol] = head;
    }
    return module2;
  }
  /** The box a reference to `name` resolves to: its last definition, if any. */
  definition(name) {
    for (let i = this.lines.length - 1; i >= 0; i--) {
      const line = this.lines[i];
      if (line.length > 1 && line[0].symbol === name)
        return line[0];
    }
    return null;
  }
  /**
   * Keep only the definitions `symbol` is built from, dropping everything else
   * the module happens to carry. Linking a library produces one big module; this
   * takes a single value back out of it.
   *
   * One backwards pass suffices: a reference resolves to a definition above it,
   * so by the time a line is reached, every line that could need it has been
   * seen. Names are kept as they are — a reference by name says which symbol was
   * used, which an id no longer does.
   */
  extract(symbol) {
    const root = this.definition(symbol);
    if (!root)
      throw new Error(`unknown symbol: ${symbol}`);
    const needed = /* @__PURE__ */ new Set([root]);
    const kept = [];
    for (let i = this.lines.length - 1; i >= 0; i--) {
      const line = this.lines[i];
      if (line.length === 1 || !needed.has(line[0]))
        continue;
      for (let j = 1; j < line.length; j++)
        needed.add(line[j]);
      kept.push(line);
    }
    const out = new _DagModule();
    out.lines = kept.reverse();
    return out;
  }
  /**
   * Namespace this module's exports under `prefix`, so that `not` compiled from
   * `bool/bool.lamb` can become `Bool.not` without colliding with any other
   * module's `not`.
   *
   * A name is exported when it is public and this is its last definition —
   * anything shadowed later was only scaffolding for what came after. Everything
   * else is prefixed *and* given a `:n` suffix, which keeps it unique across
   * modules while marking it as not part of the interface.
   *
   * Only heads are rewritten; references follow because they share the box.
   */
  qualify(prefix, options = {}) {
    const { reserved = is_label } = options;
    const last_definition = /* @__PURE__ */ new Map();
    this.lines.forEach((line, i) => {
      if (line.length === 2 && !reserved(line[0].symbol))
        last_definition.set(line[0].symbol, i);
    });
    let internal = 0;
    this.lines.forEach((line, i) => {
      const head = line[0].symbol;
      const qualifiable = (line.length === 2 || line.length === 3 && /^[a-zA-Z_]/.test(head)) && !reserved(head);
      if (!qualifiable)
        return;
      const exported = !is_private(head) && last_definition.get(head) === i;
      line[0].symbol = exported ? `${prefix}${head}` : `${prefix}${head}:${internal++}`;
    });
    return this;
  }
  /**
   * Hash-cons into a new module whose nodes have globally unique numeric ids.
   *
   * Modules compiled separately each number their nodes from scratch, so
   * concatenating them yields a DAG with the same id meaning different things in
   * different places, and with the same subtree built many times over. This
   * resolves both: every distinct node is defined exactly once, under an id no
   * other node shares.
   *
   * Sharing is keyed on resolved ids rather than on how a reference happens to
   * be spelled, so two names for one value collapse to one node.
   */
  canonicalize() {
    const LEAF_ID = 0;
    const ids = /* @__PURE__ */ new Map();
    for (const line of this.lines)
      for (const b of line)
        if (b.symbol === LEAF)
          ids.set(b, LEAF_ID);
    let next_id = 1;
    const forks = /* @__PURE__ */ new Map();
    const named = /* @__PURE__ */ new Set();
    const aliased = /* @__PURE__ */ new Map();
    const out = new _DagModule();
    const ref = (b) => {
      const id = ids.get(b);
      if (id === LEAF_ID)
        return LEAF;
      if (is_symbol_name(b.symbol) && named.has(b.symbol))
        return b.symbol;
      return id === void 0 ? b.symbol : String(id);
    };
    const key = (b) => {
      const id = ids.get(b);
      return id === void 0 ? `?${b.symbol}` : String(id);
    };
    for (const line of this.lines) {
      if (line.length === 3) {
        const [head, left, right] = line;
        const fork_key = `${key(left)} ${key(right)}`;
        const shared = forks.get(fork_key);
        if (shared !== void 0) {
          ids.set(head, shared);
        } else {
          const id = next_id++;
          forks.set(fork_key, id);
          const line_out = [box(String(id)), box(ref(left)), box(ref(right))];
          ids.set(head, id);
          out.lines.push(line_out);
        }
        if (is_label(head.symbol)) {
          const id = ids.get(head);
          out.lines.push([box(head.symbol), box(id === LEAF_ID ? LEAF : String(id))]);
        }
      } else if (line.length === 2) {
        const [head, target] = line;
        const target_id = ids.get(target);
        if (target_id !== void 0)
          ids.set(head, target_id);
        const id = ids.get(head);
        const name = head.symbol;
        const previous = aliased.get(name);
        if (previous !== void 0 && previous === id)
          continue;
        if (previous === void 0) {
          if (is_symbol_name(name))
            named.add(name);
        } else {
          named.delete(name);
        }
        aliased.set(name, id);
        out.lines.push([box(name), box(ref(target))]);
      } else if (line.length === 1) {
        out.lines.push([box(ref(line[0]))]);
      }
    }
    return out;
  }
};

// src/module/env.mjs
function environment(e, text, options = {}) {
  const { origin = "" } = options;
  const env = { [LEAF]: e.leaf };
  let context = "";
  const get = (symbol) => {
    if (symbol in env)
      return env[symbol];
    throw new Error(`${context}unbound symbol: ${symbol}`);
  };
  let line_number = 0;
  for (const line of text.split(/\r?\n/)) {
    line_number++;
    context = `${origin ? `${origin}:` : ""}${line_number}: `;
    const words = line.split(" ");
    if (words.length === 3)
      env[words[0]] = e.apply(get(words[1]), get(words[2]));
    else if (words.length === 2)
      env[words[0]] = get(words[1]);
  }
  context = "";
  return get;
}

// src/module/link.mjs
var DuplicateExportError = class extends Error {
  constructor(duplicates) {
    super("duplicate exports:\n" + duplicates.map((d) => `  ${d.symbol}: ${d.sources.join(" ")}`).join("\n"));
    this.duplicates = duplicates;
    this.name = "DuplicateExportError";
  }
};
var DependencyCycleError = class extends Error {
  constructor(cycle, edges) {
    super("dependency cycle:\n  " + [...cycle, cycle[0]].join(" \u2192 ") + (edges.length ? "\nedges:\n" + edges.map((e) => `  ${e.to} imports ${e.symbol} from ${e.from}`).join("\n") : ""));
    this.cycle = cycle;
    this.edges = edges;
    this.name = "DependencyCycleError";
  }
};
function interface_of(text) {
  const defined = /* @__PURE__ */ new Set();
  const exports2 = /* @__PURE__ */ new Set();
  const imports = /* @__PURE__ */ new Set();
  for (const raw of text.split("\n")) {
    const line = raw.trim();
    if (!line || line.startsWith("#"))
      continue;
    const words = line.split(/\s+/);
    for (let i = 1; i < words.length; i++)
      if (!defined.has(words[i]))
        imports.add(words[i]);
    defined.add(words[0]);
    if (words.length === 2 && !words[0].includes(":"))
      exports2.add(words[0]);
  }
  return { exports: [...exports2], imports: [...imports] };
}
function topological_sort(items, edges, key) {
  const by_key = /* @__PURE__ */ new Map();
  for (const item of items)
    by_key.set(key(item), item);
  const keys = [...by_key.keys()].sort();
  const indegree = /* @__PURE__ */ new Map();
  const successors = /* @__PURE__ */ new Map();
  for (const k of keys) {
    indegree.set(k, 0);
    successors.set(k, []);
  }
  for (const { from, to: to5 } of edges) {
    if (!indegree.has(to5) || !successors.has(from))
      continue;
    indegree.set(to5, indegree.get(to5) + 1);
    successors.get(from).push(to5);
  }
  const done = /* @__PURE__ */ new Set();
  const result = [];
  while (result.length < keys.length) {
    const ready = keys.find((k) => !done.has(k) && indegree.get(k) === 0);
    if (ready === void 0)
      return raise_cycle(keys.filter((k) => !done.has(k)), edges);
    result.push(by_key.get(ready));
    done.add(ready);
    for (const next of successors.get(ready))
      indegree.set(next, indegree.get(next) - 1);
  }
  return result;
}
function raise_cycle(remaining, edges) {
  const left = new Set(remaining);
  const successors = /* @__PURE__ */ new Map();
  for (const k of left)
    successors.set(k, []);
  for (const { from, to: to5 } of edges)
    if (left.has(from) && left.has(to5))
      successors.get(from).push(to5);
  const seen = /* @__PURE__ */ new Map();
  const path = [];
  let current = remaining[0];
  for (; ; ) {
    if (seen.has(current))
      throw { cycle: path.slice(seen.get(current)) };
    seen.set(current, path.length);
    path.push(current);
    const next = successors.get(current)?.[0];
    if (next === void 0)
      raise("no cycle found among stuck modules (unexpected)");
    current = next;
  }
}
function order(fragments) {
  const interfaces = new Map(fragments.map((f) => [f.name, interface_of(f.text)]));
  const exporters = /* @__PURE__ */ new Map();
  for (const f of fragments)
    for (const symbol of interfaces.get(f.name).exports)
      exporters.set(symbol, [...exporters.get(symbol) ?? [], f.name]);
  const duplicates = [...exporters].filter(([, sources]) => sources.length > 1).map(([symbol, sources]) => ({ symbol, sources }));
  if (duplicates.length)
    throw new DuplicateExportError(duplicates);
  const dependencies = [];
  for (const f of fragments)
    for (const symbol of interfaces.get(f.name).imports) {
      const from = exporters.get(symbol)?.[0];
      if (from !== void 0 && from !== f.name)
        dependencies.push({ symbol, from, to: f.name });
    }
  try {
    return topological_sort(fragments, dependencies, (f) => f.name);
  } catch (e) {
    if (!e?.cycle)
      throw e;
    const in_cycle = new Set(e.cycle);
    throw new DependencyCycleError(e.cycle, dependencies.filter((d) => in_cycle.has(d.from) && in_cycle.has(d.to)));
  }
}
function link(fragments) {
  return order(fragments).map((f) => f.text.endsWith("\n") ? f.text : f.text + "\n").join("");
}

// src/module/transform.mjs
var import_crypto = require("crypto");
var import_fs = require("fs");
var import_path = require("path");
var sha256 = (s) => (0, import_crypto.createHash)("sha256").update(s).digest("hex");
function transformer(e, program, options = {}) {
  const { cache_dir } = options;
  const m2 = marshal(e);
  let tree = null;
  const run2 = (input) => {
    tree ?? (tree = dag_default.of(e, program));
    return m2.to_string(e.apply(tree, m2.of_string(input)));
  };
  if (cache_dir === void 0)
    return run2;
  (0, import_fs.mkdirSync)(cache_dir, { recursive: true });
  const program_hash = sha256(program);
  return (input) => {
    const path = (0, import_path.resolve)(cache_dir, sha256(`${program_hash}
${input}`));
    if (!(0, import_fs.existsSync)(path))
      (0, import_fs.writeFileSync)(path, run2(input));
    return (0, import_fs.readFileSync)(path, "utf8");
  };
}

// src/format/file.mjs
var PLAUSIBLE_NAME = /^[a-zA-Z0-9][a-zA-Z0-9._-]*\.[a-zA-Z0-9]+$/;
var MAX_NAME_LENGTH = 255;
function is_plausible_file_name(name) {
  return name.length <= MAX_NAME_LENGTH && PLAUSIBLE_NAME.test(name);
}
function of_file(e, file) {
  const m2 = marshal(e);
  return e.fork(e.fork(m2.of_string(file.name), m2.of_string(file.media_type)), m2.of_buffer(file.bytes));
}
function to_file(e, x) {
  const m2 = marshal(e);
  const as_fork = e.triage(() => null, () => null, (u, v) => [u, v]);
  const outer = as_fork(x);
  if (!outer)
    return null;
  const metadata = as_fork(outer[0]);
  if (!metadata)
    return null;
  try {
    const name = m2.to_string(metadata[0]);
    if (!is_plausible_file_name(name))
      return null;
    return { name, media_type: m2.to_string(metadata[1]), bytes: m2.to_buffer(outer[1]) };
  } catch {
    return null;
  }
}

// src/main-dag.mjs
var USAGE = `Usage: dag <command> [options] [file...]

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
  --format <f>            Output format for 'eval': ${Object.keys(formatters).join(", ")}.
                          Defaults to term.

A file argument of '-', or no file at all, reads stdin.`;
var COMMANDS = ["link", "canonicalize", "qualify", "extract", "eval", "interface"];
function parse_args(argv) {
  const command = argv[0];
  if (!COMMANDS.includes(command))
    raise(`expected one of ${COMMANDS.join(", ")}, got ${command}`);
  const files = [];
  const options = { format: "term" };
  for (let i = 1; i < argv.length; i++) {
    const arg = argv[i];
    const value = () => i + 1 < argv.length ? argv[++i] : raise(`${arg} needs a value`);
    if (arg === "--prefix")
      options.prefix = value();
    else if (arg === "--reserved")
      options.reserved = value();
    else if (arg === "--symbol")
      options.symbol = value();
    else if (arg === "--format")
      options.format = value();
    else if (arg.startsWith("--"))
      raise(`unrecognized option ${arg}`);
    else
      files.push(arg);
  }
  return { command, files, options };
}
var read = (file) => (0, import_fs2.readFileSync)(file === "-" ? 0 : file, "utf8");
var read_input = (files) => read(files.length ? files[0] : "-");
function last_symbol(text) {
  let last = null;
  for (const raw of text.split(/\r?\n/)) {
    const words = raw.trim().split(/\s+/).filter(Boolean);
    if (words.length)
      last = words[0];
  }
  return last ?? raise("module is empty; nothing to evaluate");
}
function run(command, files, options) {
  const utf8 = (s) => new TextEncoder().encode(s);
  switch (command) {
    case "link":
      if (!files.length)
        raise("link needs at least one file");
      return utf8(link(files.map((name) => ({ name, text: read(name) }))));
    case "canonicalize":
      return utf8(DagModule.parse(read_input(files)).canonicalize().toString());
    case "qualify": {
      const prefix = options.prefix ?? raise("qualify needs --prefix");
      const extra = options.reserved === void 0 ? null : new RegExp(options.reserved);
      return utf8(DagModule.parse(read_input(files), { absorb_internal_aliases: false }).qualify(prefix, { reserved: (name) => is_label(name) || !!extra?.test(name) }).toString());
    }
    case "extract": {
      const symbol = options.symbol ?? raise("extract needs --symbol");
      return utf8(DagModule.parse(read_input(files)).extract(symbol).toString([symbol]));
    }
    case "eval": {
      const text = read_input(files);
      const origin = files.length && files[0] !== "-" ? files[0] : "";
      const format = formatters[options.format] ?? raise(`unrecognized format ${options.format}`);
      const value = environment(lazy_stacks_default, text, { origin })(options.symbol ?? last_symbol(text));
      const out = format.to(value);
      return options.format === "buffer" ? out : new Uint8Array([...out, 10]);
    }
    case "interface": {
      const { exports: exports2, imports } = interface_of(read_input(files));
      return utf8([
        ...exports2.map((s) => `export ${s}`),
        ...imports.map((s) => `import ${s}`)
      ].join("\n") + "\n");
    }
    default:
      return raise(`unrecognized command ${command}`);
  }
}
if (typeof require !== "undefined" && require.main === module) {
  const argv = process.argv.slice(2);
  if (!argv.length || argv[0] === "-h" || argv[0] === "--help") {
    console.log(USAGE);
  } else {
    let parsed;
    try {
      parsed = parse_args(argv);
    } catch (error) {
      console.error(`dag: ${error?.message ?? error}

${USAGE}`);
      process.exit(1);
    }
    try {
      process.stdout.write(run(parsed.command, parsed.files, parsed.options));
    } catch (error) {
      console.error(`dag ${parsed.command}: ${error?.message ?? error}`);
      process.exit(1);
    }
  }
}
// Annotate the CommonJS export names for ESM import in node:
0 && (module.exports = {
  DagModule,
  DependencyCycleError,
  DuplicateExportError,
  LEAF,
  box,
  environment,
  evaluator,
  formatters,
  interface_of,
  is_label,
  is_plausible_file_name,
  is_private,
  is_symbol_name,
  link,
  marshal,
  of_file,
  order,
  to_file,
  topological_sort,
  transformer
});
