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
  MODULE_STORE: () => MODULE_STORE,
  REDUCE_STORE: () => REDUCE_STORE,
  box: () => box,
  cache_store: () => store,
  environment: () => environment3,
  evaluator: () => lazy_stacks_default,
  fingerprint: () => fingerprint,
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
  transformer: () => transformer3
});
module.exports = __toCommonJS(main_dag_exports);
var import_fs4 = require("fs");

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
  return raise("dag representation was unexpectedly not terminated by a value");
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
    const resolve3 = (symbol) => latest[symbol] ?? (latest[symbol] = box(symbol));
    for (const raw of text.split("\n")) {
      const trimmed = raw.trim();
      if (!trimmed)
        continue;
      const words = trimmed.split(/\s+/);
      if (absorb_internal_aliases && words.length === 3 && words[1] === ":i") {
        latest[words[0]] = resolve3(words[2]);
        continue;
      }
      const is_definition = words.length === 2 || words.length === 3;
      const head = is_definition ? box(words[0]) : null;
      module2.lines.push(words.map((word, i) => i === 0 && head ? head : resolve3(word)));
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
   * Keep only the definitions `symbols` are built from, dropping everything else
   * the module happens to carry. Linking a library produces one big module; this
   * takes a value back out of it — or a set of them, sharing what they share
   * rather than repeating it, which is why it is not several extracts.
   *
   * Naming what stays is also how one drops something: what only the rest
   * reached is gone. No `drop` needed.
   *
   * One backwards pass suffices: a reference resolves to a definition above it,
   * so by the time a line is reached, every line that could need it has been
   * seen. Names are kept as they are — a reference by name says which symbol was
   * used, which an id no longer does.
   */
  extract(...symbols) {
    const needed = /* @__PURE__ */ new Set();
    for (const symbol of symbols) {
      const root = this.definition(symbol);
      if (!root)
        throw new Error(`unknown symbol: ${symbol}`);
      needed.add(root);
    }
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
   * Give every shadowed definition a name of its own.
   *
   * A reference means the definition above it, so a module can bind one name
   * twice and every reference still says which of the two it meant. That holds
   * only while the lines stay in the order they were written, and so is lost by
   * anything that regroups them. Renaming all but the last definition of each
   * name says the same thing without leaning on position — references follow,
   * because they share the box.
   */
  disambiguate() {
    const taken = /* @__PURE__ */ new Set();
    const last_definition = /* @__PURE__ */ new Map();
    for (const line of this.lines) {
      for (const b of line)
        taken.add(b.symbol);
      if (line.length > 1)
        last_definition.set(line[0].symbol, line[0]);
    }
    let n = 0;
    for (const line of this.lines) {
      const head = line[0];
      if (line.length === 1 || last_definition.get(head.symbol) === head)
        continue;
      let name;
      do {
        name = `${head.symbol}:s${n++}`;
      } while (taken.has(name));
      taken.add(name);
      head.symbol = name;
    }
  }
  /**
   * Split into what the module shares and what each of `roots` has to itself.
   *
   * A definition belongs to a root when that root is the only thing that
   * reaches it. Anything reached by two roots, or by a name outside them, stays
   * in `shared` — so concatenating `shared` with any one root's part gives back
   * exactly what `extract` would have produced for that root, and no line is
   * ever in two parts at once.
   *
   * What counts as "outside" is the module's own notion of an interface: a
   * symbolic name is something a reader can ask for, a numeric id is internal
   * scaffolding. Every named definition other than a root is therefore treated
   * as reachable, and every id is reachable only through the lines that use it.
   *
   * The point is evaluation order. A module whose expensive parts are named
   * roots evaluates all of them the moment it is read; partitioned, `shared`
   * can be read once and each root's part evaluated against it on its own — at
   * its own cost, at a time of the caller's choosing, and without repeating
   * whatever two roots have in common.
   *
   * One backwards pass suffices, for the reason `extract` gives. Shadowed
   * definitions are renamed first: a part is read after all of `shared`, so a
   * name `shared` goes on to bind again would mean the later one by then. That
   * rewrites this module in place — the parts hand back its own lines, so there
   * was never a copy to rename instead.
   */
  partition(roots) {
    this.disambiguate();
    const SHARED = Symbol("shared");
    const owner = /* @__PURE__ */ new Map();
    const claim = (b, by) => {
      const had = owner.get(b);
      owner.set(b, had === void 0 || had === by ? by : SHARED);
    };
    const wanted = new Set(roots);
    const last_definition = /* @__PURE__ */ new Map();
    this.lines.forEach((line, i) => {
      if (line.length > 1 && is_symbol_name(line[0].symbol))
        last_definition.set(line[0].symbol, i);
    });
    this.lines.forEach((line, i) => {
      const head = line[0];
      if (line.length === 1)
        return claim(head, SHARED);
      if (!is_symbol_name(head.symbol))
        return;
      const name = head.symbol;
      claim(head, wanted.has(name) && last_definition.get(name) === i ? name : SHARED);
    });
    for (let i = this.lines.length - 1; i >= 0; i--) {
      const line = this.lines[i];
      const by = owner.get(line[0]);
      if (by === void 0)
        continue;
      for (let j = 1; j < line.length; j++)
        claim(line[j], by);
    }
    const shared = new _DagModule();
    const exclusive = /* @__PURE__ */ new Map();
    for (const name of roots) {
      if (!this.definition(name))
        throw new Error(`unknown symbol: ${name}`);
      exclusive.set(name, new _DagModule());
    }
    for (const line of this.lines) {
      const by = owner.get(line[0]);
      (typeof by === "string" ? exclusive.get(by) : shared).lines.push(line);
    }
    return { shared, exclusive };
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
  const read2 = (text2, scope) => {
    const get2 = (symbol) => {
      if (symbol in scope)
        return scope[symbol];
      if (symbol in env)
        return env[symbol];
      throw new Error(`${context}unbound symbol: ${symbol}`);
    };
    let value;
    let line_number = 0;
    for (const line of text2.split(/\r?\n/)) {
      line_number++;
      context = `${origin ? `${origin}:` : ""}${line_number}: `;
      const words = line.split(" ");
      if (words.length === 3)
        scope[words[0]] = e.apply(get2(words[1]), get2(words[2]));
      else if (words.length === 2)
        scope[words[0]] = get2(words[1]);
      else if (words[0])
        value = get2(words[0]);
    }
    context = "";
    return value;
  };
  read2(text, env);
  const get = (symbol) => {
    if (symbol in env)
      return env[symbol];
    throw new Error(`unbound symbol: ${symbol}`);
  };
  get.reduce = (text2) => {
    const value = read2(text2, /* @__PURE__ */ Object.create(null));
    if (value === void 0)
      throw new Error("dag representation was not terminated by a value");
    return value;
  };
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
function memoize(run2, program, options = {}) {
  const { cache_dir: cache_dir2 } = options;
  if (cache_dir2 === void 0)
    return run2;
  (0, import_fs.mkdirSync)(cache_dir2, { recursive: true });
  const program_hash = sha256(program);
  return (input) => {
    const path = (0, import_path.resolve)(cache_dir2, sha256(`${program_hash}
${input}`));
    if (!(0, import_fs.existsSync)(path)) {
      const temporary = `${path}.${process.pid}.tmp`;
      try {
        (0, import_fs.writeFileSync)(temporary, run2(input));
        (0, import_fs.renameSync)(temporary, path);
      } catch (error) {
        (0, import_fs.rmSync)(temporary, { force: true });
        throw error;
      }
    }
    return (0, import_fs.readFileSync)(path, "utf8");
  };
}
function transformer(e, program, options = {}) {
  const m2 = marshal(e);
  let tree = null;
  return memoize((input) => {
    tree ?? (tree = dag_default.of(e, program));
    return m2.to_string(e.apply(tree, m2.of_string(input)));
  }, program, options);
}

// src/runner/native.mjs
var import_child_process = require("child_process");
var import_fs3 = require("fs");
var import_os = require("os");
var import_path3 = require("path");

// src/module/cache.mjs
var import_crypto2 = require("crypto");
var import_fs2 = require("fs");
var import_path2 = require("path");
var cache_dir = () => process.env.TREE_CALCULUS_CACHE || void 0;
var REDUCE_STORE = "reduce-v1";
var MODULE_STORE = "module-v1";
var text_key = (text) => (0, import_crypto2.createHash)("sha256").update(text).digest();
var counter = 0;
function store(name) {
  const base = cache_dir();
  if (base === void 0)
    return null;
  const directory = (0, import_path2.join)(base, name);
  (0, import_fs2.mkdirSync)(directory, { recursive: true });
  const path = (key) => (0, import_path2.join)(directory, key.toString("hex"));
  const touch = (at) => {
    try {
      (0, import_fs2.utimesSync)(at, /* @__PURE__ */ new Date(), /* @__PURE__ */ new Date());
    } catch {
    }
  };
  return {
    path,
    has: (key) => {
      const at = path(key);
      if (!(0, import_fs2.existsSync)(at))
        return false;
      touch(at);
      return true;
    },
    get: (key) => {
      const at = path(key);
      if (!(0, import_fs2.existsSync)(at))
        return null;
      touch(at);
      return (0, import_fs2.readFileSync)(at);
    },
    put: (key, data) => {
      const at = path(key);
      const temporary = `${at}.${process.pid}.${counter++}.tmp`;
      try {
        (0, import_fs2.writeFileSync)(temporary, data);
        (0, import_fs2.renameSync)(temporary, at);
      } catch (error) {
        (0, import_fs2.rmSync)(temporary, { force: true });
        throw error;
      }
      return at;
    }
  };
}

// src/module/fingerprint.mjs
var import_crypto3 = require("crypto");
var mix = (parts) => {
  const h = (0, import_crypto3.createHash)("sha256");
  for (const part of parts)
    h.update(part);
  return h.digest();
};
var LEAF_FINGERPRINT = mix([Buffer.from("tree-calculus:leaf")]);
function fingerprint(text, outer) {
  const fingerprints = /* @__PURE__ */ new Map();
  const resolve3 = (name) => {
    if (name === LEAF)
      return LEAF_FINGERPRINT;
    const own = fingerprints.get(name) ?? outer?.(name);
    if (own === void 0)
      throw new Error(`unbound symbol: ${name}`);
    return own;
  };
  let value;
  for (const line of text.split(/\r?\n/)) {
    const words = line.split(" ").filter(Boolean);
    if (words.length === 3)
      fingerprints.set(words[0], mix([resolve3(words[1]), resolve3(words[2])]));
    else if (words.length === 2)
      fingerprints.set(words[0], resolve3(words[1]));
    else if (words.length === 1)
      value = resolve3(words[0]);
  }
  return { fingerprints, value };
}

// src/runner/native.mjs
var SOURCE = "implementation/cpp/dag-machine/runner.cpp";
function once(f) {
  let value;
  return () => value === void 0 ? value = f() : value;
}
var scratch = once(() => {
  const directory = (0, import_fs3.mkdtempSync)((0, import_path3.join)((0, import_os.tmpdir)(), "tree-calculus-"));
  process.on("exit", () => (0, import_fs3.rmSync)(directory, { recursive: true, force: true }));
  return directory;
});
function source() {
  for (let directory = __dirname; ; directory = (0, import_path3.dirname)(directory)) {
    const candidate = (0, import_path3.join)(directory, SOURCE);
    if ((0, import_fs3.existsSync)(candidate))
      return candidate;
    if ((0, import_path3.dirname)(directory) === directory)
      raise(`no ${SOURCE} above ${__dirname}`);
  }
}
function source_mtime(from) {
  const headers = (0, import_path3.resolve)((0, import_path3.dirname)(from), "..");
  return (0, import_fs3.readdirSync)(headers).filter((name) => name.endsWith(".hpp")).map((name) => (0, import_fs3.statSync)((0, import_path3.join)(headers, name)).mtimeMs).reduce((a, b) => Math.max(a, b), (0, import_fs3.statSync)(from).mtimeMs);
}
var eager = () => process.env.TREE_CALCULUS_RUNNER === "eager";
var executable = once(() => {
  const from = source();
  const exe = (0, import_path3.join)((0, import_path3.dirname)(from), eager() ? "runner-eager.exe" : "runner.exe");
  const current = (0, import_fs3.existsSync)(exe) && (0, import_fs3.statSync)(exe).mtimeMs >= source_mtime(from);
  if (!current) {
    (0, import_child_process.execFileSync)(process.env.CXX ?? "c++", [
      "-O3",
      "-std=c++17",
      "-pthread",
      ...eager() ? ["-DRUNNER_EAGER"] : [],
      from,
      "-o",
      exe
    ], { stdio: "inherit" });
  }
  return exe;
});
var server = once(() => {
  const to5 = (0, import_path3.join)(scratch(), "to-runner");
  const from = (0, import_path3.join)(scratch(), "from-runner");
  (0, import_child_process.execFileSync)("mkfifo", [to5, from]);
  const write_fd = (0, import_fs3.openSync)(to5, "r+");
  const read_fd = (0, import_fs3.openSync)(from, "r+");
  const runner = (0, import_child_process.spawn)(executable(), ["-s"], { stdio: [write_fd, read_fd, "inherit"] });
  runner.unref();
  process.on("exit", () => runner.kill());
  const byte = Buffer.alloc(1);
  const line = () => {
    let text = "";
    for (; ; ) {
      if ((0, import_fs3.readSync)(read_fd, byte, 0, 1, null) === 0)
        raise("runner: no response");
      if (byte[0] === 10)
        return text;
      text += String.fromCharCode(byte[0]);
    }
  };
  const bytes = (length) => {
    const buffer = Buffer.alloc(length);
    for (let got = 0; got < length; )
      got += (0, import_fs3.readSync)(read_fd, buffer, got, length - got, null);
    return buffer;
  };
  return (command, payload) => {
    (0, import_fs3.writeSync)(write_fd, `${command}
`);
    if (payload)
      (0, import_fs3.writeSync)(write_fd, payload);
    const head = line();
    if (head === "ok")
      return Buffer.alloc(0);
    if (!head.startsWith("data "))
      raise(`runner: ${head.replace(/^err /, "")}`);
    return bytes(Number(head.slice("data ".length)));
  };
});
var loaded = null;
function ask(path, command, payload) {
  const send = server();
  if (loaded !== path) {
    loaded = null;
    send(`load ${path}`);
    loaded = path;
  }
  return send(command, payload);
}
function reduced(path, format, expression) {
  const payload = Buffer.from(expression, "utf8");
  return ask(path, `reduce ${format} ${payload.length}`, payload);
}
function bound(path, name, text) {
  const payload = Buffer.from(text, "utf8");
  ask(path, `bind ${name} ${payload.length}`, payload);
  return name;
}
function terminator(text) {
  const lines = text.split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
  const words = (lines[lines.length - 1] ?? "").split(/\s+/);
  return words.length === 1 ? words[0] : raise("dag representation was unexpectedly not terminated by a value");
}
function as_file(text, name) {
  const path = (0, import_path3.join)(scratch(), name);
  (0, import_fs3.writeFileSync)(path, text);
  return path;
}
var SIDECAR_STORE = "module-fp-v1";
function recent_dumps(modules) {
  const at = (0, import_path3.join)((0, import_path3.dirname)(modules.path(text_key(""))), "RECENT");
  const list = (0, import_fs3.existsSync)(at) ? (0, import_fs3.readFileSync)(at, "utf8").split("\n").filter(Boolean) : [];
  return {
    list,
    remember(key) {
      const next = [key.toString("hex"), ...list.filter((k) => k !== key.toString("hex"))];
      const temporary = `${at}.${process.pid}.tmp`;
      (0, import_fs3.writeFileSync)(temporary, next.slice(0, 8).join("\n") + "\n");
      (0, import_fs3.renameSync)(temporary, at);
    }
  };
}
function delta(modules, text, fingerprints) {
  const sidecars = store(SIDECAR_STORE);
  if (!sidecars)
    return null;
  const lines = text.split("\n");
  const definitions = lines.filter((line) => line.split(" ").filter(Boolean).length >= 2).length;
  if (definitions !== fingerprints.size)
    return null;
  for (const key of recent_dumps(modules).list) {
    const id = Buffer.from(key, "hex");
    const sidecar = sidecars.get(id);
    if (!sidecar || !modules.has(id))
      continue;
    const value_of = /* @__PURE__ */ new Map();
    for (const line of sidecar.toString("utf8").split("\n")) {
      const [fp, ref] = line.split(" ");
      if (ref)
        value_of.set(fp, ref);
    }
    let bindings = 0;
    let matched = 0;
    const out = [];
    for (const line of lines) {
      const words = line.split(" ").filter(Boolean);
      const known = words.length >= 2 ? value_of.get(fingerprints.get(words[0])?.toString("hex") ?? "") : void 0;
      if (words.length >= 2)
        bindings++;
      if (known === void 0) {
        out.push(line);
      } else {
        matched++;
        out.push(`${words[0]} ${known}`);
      }
    }
    if (matched * 2 < bindings)
      continue;
    const dump = modules.get(id).toString("utf8");
    const structure = dump.split("\n").filter((line) => line.startsWith("~"));
    return structure.join("\n") + "\n" + out.join("\n");
  }
  return null;
}
function sidecar_of(dump, fingerprints) {
  const ref_of = /* @__PURE__ */ new Map();
  for (const line of dump.toString("utf8").split("\n")) {
    const words = line.split(" ");
    if (words.length === 2 && !words[0].startsWith("~"))
      ref_of.set(words[0], words[1]);
  }
  const out = [];
  for (const [name, fp] of fingerprints) {
    const ref = ref_of.get(name);
    if (ref)
      out.push(`${fp.toString("hex")} ${ref}`);
  }
  return out.join("\n") + "\n";
}
function loadable(text, name) {
  return once(() => {
    const modules = eager() ? store(MODULE_STORE) : null;
    if (!modules || /^~/m.test(text))
      return as_file(text, name);
    const key = text_key(text);
    if (modules.has(key))
      return modules.path(key);
    const raw = as_file(text, name);
    try {
      const fingerprints = fingerprint(text).fingerprints;
      let used = raw;
      let dump;
      try {
        const patched = delta(modules, text, fingerprints);
        if (patched !== null)
          used = as_file(patched, `delta-${name}`);
        dump = ask(used, "dump");
      } catch (error) {
        if (used === raw)
          throw error;
        used = raw;
        dump = ask(raw, "dump");
      }
      const final = modules.put(key, dump);
      store(SIDECAR_STORE)?.put(key, sidecar_of(dump, fingerprints));
      recent_dumps(modules).remember(key);
      if (loaded === used)
        loaded = final;
      return final;
    } catch {
      return raw;
    }
  });
}
function answered(named, key, miss) {
  const st = store(named);
  if (!st)
    return miss();
  let fp;
  try {
    fp = key();
  } catch {
    fp = void 0;
  }
  if (!fp)
    return miss();
  const hit = st.get(fp);
  if (hit)
    return hit;
  const answer = miss();
  st.put(fp, answer);
  return answer;
}
function transformer2(_, program, options = {}) {
  const path = loadable(program, "program.dag");
  const symbol = once(() => terminator(program));
  return memoize((input) => {
    const argument = bound(path(), "~input", input);
    return reduced(path(), "string", `~result ${symbol()} ${argument}
~result
`).toString("utf8");
  }, program, options);
}
function environment2(e, text, _ = {}) {
  const path = loadable(text, "module.dag");
  const of_answer = (answer) => dag_default.of(e, answer.toString("utf8"));
  const fingerprints = once(() => fingerprint(text).fingerprints);
  const get = (symbol) => of_answer(answered(REDUCE_STORE, () => fingerprints().get(symbol), () => reduced(path(), "dag", `${symbol}
`)));
  get.reduce = (text2) => of_answer(answered(REDUCE_STORE, () => fingerprint(text2, (name) => fingerprints().get(name)).value, () => reduced(path(), "dag", text2)));
  return get;
}
var native = ["1", "eager"].includes(process.env.TREE_CALCULUS_RUNNER ?? "") ? { transformer: transformer2, environment: environment2 } : null;

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
var environment3 = native?.environment ?? environment;
var transformer3 = native?.transformer ?? transformer;
var USAGE = `Usage: dag <command> [options] [file...]

Commands:
  link <file>...          Concatenate modules in dependency order. Rejects
                          duplicate exports and dependency cycles.
  canonicalize [file]     Hash-cons into globally unique numeric ids.
  qualify --prefix <p> [file]
                          Namespace a module's exports under <p>. Definitions
                          that are not exported are made unique but stay private.
  extract --symbol <s>... [file]
                          Keep only what the named symbols are built from, as a
                          DAG naming them. Several share what they share.
  eval [file]             Evaluate a module and print one of its symbols.
  interface [file]        List what a module exports and what it needs.

Options:
  --prefix <p>            Namespace prefix for 'qualify', e.g. 'Bool.'
  --reserved <regex>      Names 'qualify' must leave alone, on top of labels.
  --symbol <s>            Which symbol 'extract' keeps \u2014 repeat it for several \u2014
                          or which one 'eval' prints. 'eval' defaults to the
                          last one.
  --matching <regex>      'extract's symbols by pattern; '^Nat\\.' is a module.
  --except <regex>        The same, by what they are not.
  --format <f>            Output format for 'eval': ${Object.keys(formatters).join(", ")}.
                          Defaults to term.

A file argument of '-', or no file at all, reads stdin.`;
var COMMANDS = ["link", "canonicalize", "qualify", "extract", "eval", "interface"];
function parse_args(argv) {
  const command = argv[0];
  if (!COMMANDS.includes(command))
    raise(`expected one of ${COMMANDS.join(", ")}, got ${command}`);
  const files = [];
  const options = { symbols: [], format: "term" };
  for (let i = 1; i < argv.length; i++) {
    const arg = argv[i];
    const value = () => i + 1 < argv.length ? argv[++i] : raise(`${arg} needs a value`);
    if (arg === "--prefix")
      options.prefix = value();
    else if (arg === "--reserved")
      options.reserved = value();
    else if (arg === "--symbol")
      options.symbols.push(value());
    else if (arg === "--matching")
      options.matching = value();
    else if (arg === "--except")
      options.except = value();
    else if (arg === "--format")
      options.format = value();
    else if (arg.startsWith("--"))
      raise(`unrecognized option ${arg}`);
    else
      files.push(arg);
  }
  return { command, files, options };
}
var read = (file) => (0, import_fs4.readFileSync)(file === "-" ? 0 : file, "utf8");
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
      const module2 = DagModule.parse(read_input(files));
      const named = () => [...new Set(module2.lines.filter((line) => line.length > 1 && is_symbol_name(line[0].symbol)).map((line) => line[0].symbol))];
      const by_pattern = (pattern, wanted) => pattern === void 0 ? [] : named().filter((name) => new RegExp(pattern).test(name) === wanted);
      const symbols = [.../* @__PURE__ */ new Set([
        ...options.symbols,
        ...by_pattern(options.matching, true),
        ...by_pattern(options.except, false)
      ])];
      if (!symbols.length)
        raise("extract needs --symbol, --matching or --except");
      return utf8(module2.extract(...symbols).toString(symbols.length === 1 ? symbols : []));
    }
    case "eval": {
      const text = read_input(files);
      const origin = files.length && files[0] !== "-" ? files[0] : "";
      const format = formatters[options.format] ?? raise(`unrecognized format ${options.format}`);
      const value = environment3(lazy_stacks_default, text, { origin })(options.symbols.at(-1) ?? last_symbol(text));
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
  MODULE_STORE,
  REDUCE_STORE,
  box,
  cache_store,
  environment,
  evaluator,
  fingerprint,
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
