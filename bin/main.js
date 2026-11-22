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

// src/main.mjs
var main_exports = {};
__export(main_exports, {
  children: () => children,
  evaluator: () => lazy_stacks_default,
  formatter_dag: () => dag_default,
  formatter_minbin: () => minbin_default,
  formatter_readable: () => readable_default,
  formatter_ternary: () => ternary_default,
  formatters: () => formatters,
  id: () => id,
  marshal: () => m
});
module.exports = __toCommonJS(main_exports);

// src/common.mjs
var import_worker_threads = require("worker_threads");
var writer = `${process.pid}.${import_worker_threads.threadId}`;
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
function id(e) {
  return e.fork(e.stem(e.stem(e.leaf)), e.leaf);
}

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
  const id2 = e.fork(e.stem(e.stem(e.leaf)), e.leaf);
  const stack = [id2];
  const apply = (x) => stack[stack.length - 1] = e.apply(stack[stack.length - 1] || raise("unmatched parentheses"), x);
  for (const c of s) {
    switch (c) {
      case "\u25B3":
        apply(e.leaf);
        break;
      case "(":
        stack.push(id2);
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
      s.push(y.length > 2 ? [z, [], y, []] : [z, ...y], z, ...x[0]);
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

// src/format/dagz.mjs
var SQUASH_TABLE = [
  1,
  2,
  3,
  6,
  10,
  16,
  27,
  45,
  73,
  120,
  194,
  310,
  488,
  747,
  1101,
  1546,
  2047,
  2549,
  2994,
  3348,
  3607,
  3785,
  3901,
  3975,
  4022,
  4050,
  4068,
  4079,
  4085,
  4089,
  4092,
  4093,
  4094
];
function squash(d) {
  if (d > 2047)
    return 4095;
  if (d < -2047)
    return 1;
  const w = d & 127, i = (d >> 7) + 16;
  return SQUASH_TABLE[i] * (128 - w) + SQUASH_TABLE[i + 1] * w + 64 >> 7;
}
var STRETCH = new Int16Array(4096);
{
  let pi = 0;
  for (let x = -2047; x <= 2047; x++) {
    const v = squash(x);
    for (let j = pi; j <= v; j++)
      STRETCH[j] = x;
    pi = v + 1;
  }
  for (let j = pi; j < 4096; j++)
    STRETCH[j] = 2047;
}
var LIMIT = 30;
var RATE = Array.from({ length: LIMIT + 1 }, (_, n) => Math.floor(131072 / (2 * n + 3)));
var WEIGHT = 13107;
var BIAS = 256;
var Model = class {
  constructor() {
    this.contexts = /* @__PURE__ */ new Map();
    this.weights = /* @__PURE__ */ new Map();
    this.used = [];
    this.inputs = [];
    this.mixer = [];
    this.p = 2048;
  }
  predict(contexts, selector) {
    this.used = contexts.map((c) => {
      let e = this.contexts.get(c);
      if (!e)
        this.contexts.set(c, e = [32768, 0]);
      return e;
    });
    this.inputs = [...this.used.map((e) => STRETCH[e[0] >> 4]), BIAS];
    let w = this.weights.get(selector);
    if (!w)
      this.weights.set(selector, w = [...contexts.map(() => WEIGHT), 0]);
    this.mixer = w;
    let dot = 0;
    for (let i = 0; i < w.length; i++)
      dot += w[i] * this.inputs[i];
    this.p = Math.min(Math.max(squash(Math.floor(dot / 65536)), 1), 4095);
    return this.p;
  }
  update(bit) {
    const err = (bit << 12) - this.p;
    for (let i = 0; i < this.mixer.length; i++)
      this.mixer[i] += this.inputs[i] * err >> 10;
    for (const e of this.used) {
      e[0] += Math.floor(((bit << 16) - e[0]) * RATE[e[1]] / 65536);
      if (e[1] < LIMIT)
        e[1]++;
    }
  }
};
var Decoder = class {
  constructor(bytes) {
    this.bytes = bytes;
    this.x1 = 0;
    this.x2 = 4294967295;
    this.x = 0;
    this.pos = 0;
    for (let i = 0; i < 4; i++)
      this.x = (this.x << 8 | this.byte()) >>> 0;
  }
  byte() {
    return this.pos < this.bytes.length ? this.bytes[this.pos++] : (this.pos++, 0);
  }
  code(_, p) {
    const xm = this.x1 + (this.x2 - this.x1 >>> 12) * p;
    const bit = this.x <= xm ? 1 : 0;
    if (bit)
      this.x2 = xm;
    else
      this.x1 = xm + 1;
    while (((this.x1 ^ this.x2) & 4278190080) === 0) {
      this.x1 = this.x1 << 8 >>> 0;
      this.x2 = (this.x2 << 8 | 255) >>> 0;
      this.x = (this.x << 8 | this.byte()) >>> 0;
    }
    return bit;
  }
};
function walk(code, known) {
  const model = new Model();
  const history = [];
  const decide = (tag, extra, s, bit) => {
    const h = (n) => history.slice(-n).join();
    const contexts = [
      `0${s.side}`,
      `1${s.side}${s.spine}.${s.heads.slice(0, 1).join()}`,
      `2${s.path}`,
      `3${h(8)}`,
      `4${s.side}${s.spine}`,
      `5${s.side}${s.spine}.${s.heads.slice(0, 2).join()}`
    ];
    const p = model.predict(contexts.map((c) => `${tag}${extra};${c}`), tag + extra.split(".")[0]);
    const result = code(bit, p);
    model.update(result);
    return result;
  };
  const nodes = [[0, 0]];
  const head = [{ token: "L", args: 0 }];
  const written = /* @__PURE__ */ new Map();
  const child = (x, s) => {
    if (decide("l", "", s, x === 0 ? 1 : 0)) {
      history.push("L");
      return 0;
    }
    const n = nodes.length - 1;
    if (decide("r", "", s, x !== null && written.has(x) ? 1 : 0)) {
      const i = x === null ? 0 : written.get(x);
      const width = n > 1 ? (n - 1).toString(2).length : 1;
      let v = 0;
      for (let k = width - 1; k >= 0; k--)
        v = v * 2 + decide("t", `${k}.${v}`, s, i >> k & 1);
      if (v >= n)
        raise("dagz: reference to a node not yet written");
      history.push(`R${v}`);
      head[v + 1] = { token: `R${v}`, args: 0 };
      return v + 1;
    }
    history.push("N");
    const [l, r] = x === null ? [null, null] : known.nodes[x];
    const left = child(l, { side: "L", spine: s.side === "L" ? s.spine + 1 : 1, heads: s.heads, path: (s.path + "L").slice(-4) });
    const { token, args } = head[left];
    const right = child(r, { side: "R", spine: 0, heads: [`${token}.${args}`, ...s.heads.slice(0, 3)], path: (s.path + "R").slice(-4) });
    nodes.push([left, right]);
    head.push({ token, args: args + 1 });
    if (x !== null)
      written.set(x, nodes.length - 2);
    return nodes.length - 1;
  };
  const root = child(known && known.root, { side: "R", spine: 0, heads: [], path: "" });
  return { nodes, root };
}
function decode(bytes) {
  const decoder = new Decoder(bytes);
  const dag = walk((bit, p) => decoder.code(bit, p), null);
  return { dag, tail: bytes.slice(decoder.pos) };
}
function build(bytes, leaf, apply) {
  const { dag, tail } = decode(bytes);
  const built = [leaf];
  for (let i = 1; i < dag.nodes.length; i++)
    built.push(apply(built[dag.nodes[i][0]], built[dag.nodes[i][1]]));
  const one = apply(leaf, leaf);
  let list = leaf;
  for (let i = tail.length * 8 - 1; i >= 0; i--)
    list = apply(apply(leaf, tail[i >> 3] >> 7 - (i & 7) & 1 ? one : leaf), list);
  return apply(built[dag.root], list);
}
function of5(e, bytes) {
  return build(bytes, e.leaf, (a, b) => e.apply(a, b));
}

// src/format/formats.mjs
var text_enc = new TextEncoder();
var text_dec = new TextDecoder();
var m = marshal(lazy_stacks_default);
var of_marshaller = (of6, to5, of_string, to_string) => ({
  of: (s) => of6(of_string(text_dec.decode(s))),
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
  minbin: of_formatter(minbin_default),
  dagz: {
    of: (s) => of5(lazy_stacks_default, s),
    to: (_) => raise("dagz is read-only here: a dagz file is its root applied to its raw bytes, which a tree does not say")
  }
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

// src/main.mjs
var import_fs = require("fs");
if (typeof require !== "undefined" && require.main === module) {
  const args = process.argv.slice(2);
  let input_mode_file = false;
  let current_format = "infer";
  let last_format = "term";
  let current_value = id(lazy_stacks_default);
  for (const raw_arg of args) {
    if (raw_arg.startsWith("-") && raw_arg.length > 1) {
      const arg = raw_arg.replace(/^-+/, "");
      if (arg === "file")
        input_mode_file = true;
      else if (arg in formatters_infer)
        last_format = current_format = arg;
      else
        raise(`unrecognized format ${arg}`);
    } else {
      const content = raw_arg === "-" ? new Uint8Array((0, import_fs.readFileSync)(0)) : input_mode_file ? new Uint8Array((0, import_fs.readFileSync)(raw_arg)) : text_enc.encode(raw_arg);
      input_mode_file = false;
      const [value, format] = formatters_infer[current_format](content);
      last_format = format;
      current_value = lazy_stacks_default.apply(current_value, value);
    }
  }
  if (last_format == "buffer")
    process.stdout.write(formatters[last_format].to(current_value));
  else
    console.log(text_dec.decode(formatters[last_format].to(current_value)));
}
// Annotate the CommonJS export names for ESM import in node:
0 && (module.exports = {
  children,
  evaluator,
  formatter_dag,
  formatter_minbin,
  formatter_readable,
  formatter_ternary,
  formatters,
  id,
  marshal
});
