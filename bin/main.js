#!/usr/bin/env node
"use strict";

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
  const to_string = (t) => to_list(t).map(to_nat).map((x) => String.fromCharCode(Number(x))).join("");
  const of_string = (s) => of_list(s.split("").map((c) => of_nat(BigInt(c.charCodeAt(0)))));
  return {
    to_bool,
    of_bool,
    to_list,
    of_list,
    to_nat,
    of_nat,
    to_string,
    of_string
  };
}
function id(e) {
  return e.fork(e.stem(e.stem(e.leaf)), e.leaf);
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

// src/main.mjs
var import_fs = require("fs");
var m = marshal(lazy_stacks_default);
var of_marshaller = (of4, to4, of_string, to_string) => ({
  of: (s) => of4(of_string(s)),
  to: (x) => to_string(to4(x))
});
var of_formatter = (f) => ({
  of: (s) => f.of(lazy_stacks_default, s),
  to: (x) => f.to(lazy_stacks_default, x)
});
var formatters = {
  bool: of_marshaller(m.of_bool, m.to_bool, (s) => s === "true" ? true : s === "false" ? false : raise("invalid boolean"), (x) => x ? "true" : "false"),
  nat: of_marshaller(m.of_nat, m.to_nat, (s) => BigInt(s), (x) => x.toString()),
  string: of_marshaller(m.of_string, m.to_string, (s) => s, (x) => x),
  ternary: of_formatter(ternary_default),
  dag: of_formatter(dag_default),
  term: of_formatter(readable_default)
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
  return guess("bool") || guess("ternary") || guess("nat") || guess("term") || guess("dag") || guess("string") || raise(`could not infer format`);
};
var formatters_infer = {};
for (const format in formatters)
  formatters_infer[format] = (s) => [formatters[format].of(s), format];
formatters_infer["infer"] = parse_infer;
var args = process.argv.slice(2);
var input_mode_file = false;
var current_format = "infer";
var last_format = "term";
var current_value = id(lazy_stacks_default);
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
    const content = raw_arg === "-" ? (0, import_fs.readFileSync)(0, "utf8").trimEnd() : input_mode_file ? (0, import_fs.readFileSync)(raw_arg, "utf8").trimEnd() : raw_arg;
    input_mode_file = false;
    const [value, format] = formatters_infer[current_format](content);
    last_format = format;
    current_value = lazy_stacks_default.apply(current_value, value);
  }
}
console.log(formatters[last_format].to(current_value));
