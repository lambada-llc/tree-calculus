#!/usr/bin/env node
"use strict";
var __defProp = Object.defineProperty;
var __getOwnPropDesc = Object.getOwnPropertyDescriptor;
var __getOwnPropNames = Object.getOwnPropertyNames;
var __hasOwnProp = Object.prototype.hasOwnProperty;
var __esm = (fn, res) => function __init() {
  return fn && (res = (0, fn[__getOwnPropNames(fn)[0]])(fn = 0)), res;
};
var __export = (target, all) => {
  for (var name in all)
    __defProp(target, name, { get: all[name], enumerable: true });
};
var __copyProps = (to4, from, except, desc) => {
  if (from && typeof from === "object" || typeof from === "function") {
    for (let key of __getOwnPropNames(from))
      if (!__hasOwnProp.call(to4, key) && key !== except)
        __defProp(to4, key, { get: () => from[key], enumerable: !(desc = __getOwnPropDesc(from, key)) || desc.enumerable });
  }
  return to4;
};
var __toCommonJS = (mod) => __copyProps(__defProp({}, "__esModule", { value: true }), mod);

// src/common.ts
var common_exports = {};
__export(common_exports, {
  assert_equal: () => assert_equal,
  children: () => children,
  id: () => id,
  marshal: () => marshal,
  measure: () => measure,
  raise: () => raise
});
function assert_equal(expected, actual, test_case) {
  console.assert(expected === actual, `expected: ${expected}, actual: ${actual}, test: ${test_case}`);
}
function measure(f) {
  const before_ms = Date.now();
  const result = f();
  const after_ms = Date.now();
  const elasped_ms = after_ms - before_ms;
  return { result, elasped_ms };
}
function children(e, x) {
  return e.triage(
    () => [],
    (u) => [u],
    (u, v) => [u, v]
  )(x);
}
function marshal(e) {
  const t_false = e.leaf;
  const t_true = e.stem(e.leaf);
  const to_bool = e.triage(() => false, (_) => true, (_) => raise("tree is not a bool"));
  const of_bool = (b) => b ? t_true : t_false;
  const to_list = (t) => {
    let l = [];
    const triage = e.triage(() => false, (_) => raise("tree is not a list"), (hd, tl) => (l.push(hd), t = tl, true));
    while (triage(t)) ;
    return l;
  };
  const of_list = (l) => {
    let f = e.leaf;
    for (let i = l.length; i; i--) f = e.fork(l[i - 1], f);
    return f;
  };
  const to_nat = (t) => to_list(t).reduceRight((acc, b) => 2n * acc + (to_bool(b) ? 1n : 0n), 0n);
  const of_nat = (n) => {
    let l = [];
    for (; n; n >>= 1n) l.push(of_bool(n % 2n == 1n));
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
var raise;
var init_common = __esm({
  "src/common.ts"() {
    "use strict";
    raise = (message) => {
      throw new Error(message);
    };
  }
});

// src/evaluator/eager-stacks.ts
var eager_stacks_exports = {};
__export(eager_stacks_exports, {
  default: () => eager_stacks_default
});
function reduce(expression) {
  const todo = [expression];
  while (todo.length)
    reduceOne(todo);
  return expression;
}
var reduceOne, evaluator, eager_stacks_default;
var init_eager_stacks = __esm({
  "src/evaluator/eager-stacks.ts"() {
    "use strict";
    reduceOne = (todo) => {
      const s = todo.pop();
      if (s.length < 3) return;
      todo.push(s);
      const x = s.pop(), y = s.pop(), z = s.pop();
      if (x.length === 0) s.push(...y);
      else if (x.length === 1) {
        const newPotRedex = [z, ...y];
        s.push(newPotRedex, z, ...x[0]);
        todo.push(newPotRedex);
      } else if (x.length === 2) {
        if (z.length === 0) s.push(...x[1]);
        else if (z.length === 1) s.push(z[0], ...x[0]);
        else if (z.length === 2) s.push(z[0], z[1], ...y);
      }
    };
    evaluator = {
      // construct
      leaf: [],
      stem: (u) => [u],
      fork: (u, v) => [v, u],
      // eval
      apply: (a, b) => reduce([b, ...a]),
      // destruct
      triage: (on_leaf, on_stem, on_fork) => (x) => {
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
    eager_stacks_default = evaluator;
  }
});

// src/format/dag.ts
var dag_exports = {};
__export(dag_exports, {
  default: () => dag_default
});
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
    if (c) env[a] = e.apply(get_env(b), get_env(c));
    else if (b) env[a] = get_env(b);
    else if (a) return get_env(a);
  }
  return raise("dag representation was unepxectedly not terminated by a value");
}
var formatter, dag_default;
var init_dag = __esm({
  "src/format/dag.ts"() {
    "use strict";
    init_common();
    formatter = { to, of };
    dag_default = formatter;
  }
});

// src/format/ternary.ts
var ternary_exports = {};
__export(ternary_exports, {
  default: () => ternary_default,
  of: () => of2,
  to: () => to2
});
function to2(e, x) {
  const res = [];
  const triage = e.triage(
    () => res.push("0"),
    (u) => (res.push("1"), triage(u)),
    (u, v) => (res.push("2"), triage(u), triage(v))
  );
  triage(x);
  return res.join("");
}
function of2(e, s) {
  const stack = s.split("").reverse();
  const f = () => {
    switch (stack.pop()) {
      case "0":
        return e.leaf;
      case "1":
        return e.stem(f());
      case "2":
        return e.fork(f(), f());
      default:
        return raise("unexpected character in ternary encoding");
    }
  };
  return f();
}
var formatter2, ternary_default;
var init_ternary = __esm({
  "src/format/ternary.ts"() {
    "use strict";
    init_common();
    formatter2 = { to: to2, of: of2 };
    ternary_default = formatter2;
  }
});

// src/format/readable.ts
var readable_exports = {};
__export(readable_exports, {
  default: () => readable_default,
  of: () => of3,
  to: () => to3
});
function to3(e, x) {
  const triage = e.triage(
    () => "\u25B3",
    (u) => `(\u25B3 ${triage(u)})`,
    (u, v) => `(\u25B3 ${triage(u)} ${triage(v)})`
  );
  return e.triage(
    () => "\u25B3",
    (u) => `\u25B3 ${triage(u)}`,
    (u, v) => `\u25B3 ${triage(u)} ${triage(v)}`
  )(x);
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
var formatter3, readable_default;
var init_readable = __esm({
  "src/format/readable.ts"() {
    "use strict";
    init_common();
    formatter3 = { to: to3, of: of3 };
    readable_default = formatter3;
  }
});

// src/main.js
Object.defineProperty(exports, "__esModule", { value: true });
var common_1 = (init_common(), __toCommonJS(common_exports));
var eager_stacks_1 = (init_eager_stacks(), __toCommonJS(eager_stacks_exports));
var dag_1 = (init_dag(), __toCommonJS(dag_exports));
var ternary_1 = (init_ternary(), __toCommonJS(ternary_exports));
var readable_1 = (init_readable(), __toCommonJS(readable_exports));
var m = (0, common_1.marshal)(eager_stacks_1.default);
var of_marshaller = (of4, to4, of_string, to_string) => ({
  of: (s) => of4(of_string(s)),
  to: (x) => to_string(to4(x))
});
var of_formatter = (f) => ({
  of: (s) => f.of(eager_stacks_1.default, s),
  to: (x) => f.to(eager_stacks_1.default, x)
});
var formatters = {
  bool: of_marshaller(m.of_bool, m.to_bool, (s) => s === "true" ? true : s === "false" ? false : (0, common_1.raise)("invalid boolean"), (x) => x ? "true" : "false"),
  nat: of_marshaller(m.of_nat, m.to_nat, (s) => BigInt(s), (x) => x.toString()),
  string: of_marshaller(m.of_string, m.to_string, (s) => s, (x) => x),
  ternary: of_formatter(ternary_1.default),
  dag: of_formatter(dag_1.default),
  term: of_formatter(readable_1.default)
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
  return guess("bool") || guess("ternary") || guess("nat") || guess("term") || guess("dag") || guess("string") || (0, common_1.raise)(`could not infer format`);
};
var formatters_infer = {};
for (const format in formatters)
  formatters_infer[format] = (s) => [formatters[format].of(s), format];
formatters_infer["infer"] = parse_infer;
var args = process.argv.slice(2);
var input_mode_file = false;
var current_format = "infer";
var last_format = "term";
var current_value = (0, common_1.id)(eager_stacks_1.default);
for (const raw_arg of args) {
  if (raw_arg.startsWith("-") && raw_arg.length > 1) {
    const arg = raw_arg.replace(/^-+/, "");
    if (arg === "file")
      input_mode_file = true;
    else if (arg in formatters_infer)
      last_format = current_format = arg;
    else
      (0, common_1.raise)(`unrecognized format ${arg}`);
  } else {
    const content = input_mode_file ? require("fs").readFileSync(raw_arg === "-" ? 0 : raw_arg, "utf8").trimEnd() : raw_arg;
    input_mode_file = false;
    const [value, format] = formatters_infer[current_format](content);
    last_format = format;
    current_value = eager_stacks_1.default.apply(current_value, value);
  }
}
console.log(formatters[last_format].to(current_value));
