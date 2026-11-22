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
  1,
  1,
  2,
  3,
  5,
  8,
  13,
  22,
  36,
  60,
  98,
  162,
  267,
  439,
  720,
  1179,
  1921,
  3108,
  4971,
  7812,
  11955,
  17625,
  24743,
  32768,
  40793,
  47911,
  53581,
  57724,
  60565,
  62428,
  63615,
  64357,
  64816,
  65097,
  65269,
  65374,
  65438,
  65476,
  65500,
  65514,
  65523,
  65528,
  65531,
  65533,
  65534,
  65535,
  65535,
  65535
];
var LOGIT = 3071;
function squash(d) {
  if (d >= 3072)
    return 65535;
  if (d <= -3072)
    return 1;
  const i = d + 3072 >> 7, w = d + 3072 & 127;
  const v = SQUASH_TABLE[i] * (128 - w) + SQUASH_TABLE[i + 1] * w + 64 >> 7;
  return v < 1 ? 1 : v > 65535 ? 65535 : v;
}
var STRETCH = new Int16Array(65536);
{
  let pi = 0;
  for (let d = -LOGIT; d <= LOGIT; d++) {
    const v = squash(d);
    for (let j = pi; j <= v; j++)
      STRETCH[j] = d;
    pi = v + 1;
  }
  for (let j = pi; j < 65536; j++)
    STRETCH[j] = LOGIT;
}
var clamp = (d) => d > LOGIT ? LOGIT : d < -LOGIT ? -LOGIT : d;
var mix = (h, v) => {
  h = Math.imul(h + v | 0, 2654435761);
  return (h ^ h >>> 15) >>> 0;
};
var fin = (h) => {
  h ^= h >>> 13;
  h = Math.imul(h, 2246822507);
  return (h ^ h >>> 16) >>> 0;
};
var UNKNOWN = 0;
var LEAF = 1;
var NEW = 2;
var REF = 3;
var X = 0;
var H = 1;
var T = 2;
var KINDS = 3;
var TAGS = 32;
var SETS = TAGS * KINDS;
var BASE = 6;
var TABLE_BITS = 24;
var RATE = Array.from({ length: 256 }, (_, n) => Math.floor(131072 / (2 * n + 3)));
var NX = [-1, 0, -1, 1, -2, 0, -2, 2, -1, 1, -3, 0, -2, 2, 3, -3];
var NY = [0, -1, -1, -1, 0, -2, -1, -1, -2, -2, 0, -3, -2, -2, -1, -1];
var MANTISSA = [32768, 35734, 38968, 42495, 46341, 50535, 55109, 60097];
var value = (k) => Math.floor(MANTISSA[k & 7] * 2 ** (k >> 3) / 32768);
var CODED = ["fast", "weight", "slowWeight", "slowScale", "slowLimit", "rate", "decay", "minRate"];
var DEFAULTS = {
  shapes: [[1, 1], [2, 2], [3, 3], [4, 4], [6, 6], [8, 1], [1, 8], [16, 0], [0, 16], [2, 4], [4, 2]],
  fast: 56,
  weight: 120,
  slowWeight: 104,
  slowScale: 40,
  slowLimit: 64,
  rate: 136,
  decay: 48,
  minRate: 120,
  learn: 11,
  apmRate: 6,
  apmWeight: 2
};
function header(code, given) {
  const put = (v, bits) => {
    let r = 0;
    for (let i = bits - 1; i >= 0; i--)
      r = r * 2 + code(v >> i & 1, 32768);
    return r;
  };
  if (!put(given ? 1 : 0, 1))
    return DEFAULTS;
  const shapes = [];
  const n = put(given ? given.shapes.length : 0, 5);
  for (let i = 0; i < n; i++)
    shapes.push([put(given ? given.shapes[i][0] : 0, 5), put(given ? given.shapes[i][1] : 0, 5)]);
  const p = { shapes };
  for (const k of CODED)
    p[k] = put(given ? given[k] : 0, 8);
  p.learn = put(given ? given.learn : 0, 4);
  p.apmRate = put(given ? given.apmRate : 0, 4);
  p.apmWeight = put(given ? given.apmWeight : 0, 2);
  return p;
}
var Model = class {
  constructor(p) {
    this.check = new Uint8Array(1 << TABLE_BITS);
    this.fast = new Int16Array(1 << TABLE_BITS);
    this.fastN = new Uint8Array(1 << TABLE_BITS);
    this.slow = new Uint16Array(1 << TABLE_BITS);
    this.slowN = new Uint8Array(1 << TABLE_BITS);
    this.w1Set = new Uint8Array(SETS + 65536);
    this.w1Uses = new Uint16Array(SETS + 65536);
    this.w2Set = new Uint8Array(SETS * 16);
    this.apm = new Uint16Array(SETS * 4096 * 25);
    this.apmSet = new Uint8Array(SETS * 4096);
    this.parent = [-1];
    this.at = [0];
    this.atKind = [0];
    this.outcome = [[[], [], []]];
    this.nested = [[[], [], []]];
    this.history = new Int32Array(8);
    this.count = 0;
    this.pattern = 0;
    this.grid = false;
    this.kind = 0;
    this.shapes = p.shapes;
    this.contexts = BASE + p.shapes.length;
    this.inputs = 2 * this.contexts + 1;
    this.reach = Math.max(3, ...p.shapes.map(([, ry]) => ry));
    this.fastMax = value(p.fast);
    this.w0 = value(p.weight);
    this.w0slow = value(p.slowWeight);
    this.slowScale = value(p.slowScale);
    this.slowLimit = Math.min(value(p.slowLimit), 255);
    this.rate = value(p.rate);
    this.decay = value(p.decay);
    this.minRate = value(p.minRate);
    this.learnShift = p.learn;
    this.apmRate = p.apmRate;
    this.apmWeight = p.apmWeight;
    this.ctx = new Uint32Array(this.contexts);
    this.w1 = new Int32Array((SETS + 65536) * this.inputs);
    this.w2 = new Int32Array(SETS * 16 * this.inputs);
  }
  intern(list, kind, i) {
    const existing = this.nested[list][kind][i];
    if (existing !== void 0)
      return existing;
    const id2 = this.parent.length;
    this.parent.push(list);
    this.at.push(i);
    this.atKind.push(kind);
    this.outcome.push([[], [], []]);
    this.nested.push([[], [], []]);
    this.nested[list][kind][i] = id2;
    return id2;
  }
  seen(list, kind, i) {
    return list < 0 || i < 0 ? UNKNOWN : this.outcome[list][kind][i] ?? UNKNOWN;
  }
  record(s, o) {
    if (s.kind !== X)
      this.outcome[s.list][s.kind][s.index] = o;
    this.history[this.count++ & 7] = o;
  }
  enter(s) {
    let hh = 0;
    for (let i = 0; i < 8; i++)
      hh = mix(hh, this.history[this.count - 1 - i & 7] + 1);
    const c = this.ctx;
    c[0] = mix(mix(1, s.side), s.kind);
    c[1] = mix(mix(mix(mix(2, s.side), s.spine), s.h0), s.kind);
    c[2] = mix(mix(mix(3, s.path), s.plen), s.kind);
    c[3] = mix(mix(4, hh), s.kind);
    c[4] = mix(mix(mix(5, s.side), s.spine), s.kind);
    c[5] = mix(mix(mix(mix(mix(6, s.side), s.spine), s.h0), s.h1), s.kind);
    this.pattern = 0;
    this.grid = s.kind !== X && this.parent[s.list] >= 0;
    if (this.grid) {
      const p = this.parent[s.list], j = this.at[s.list], pk = this.atKind[s.list];
      const rows = [s.list];
      for (let dy = 1; dy <= this.reach; dy++)
        rows.push(j - dy < 0 ? -1 : this.nested[p][pk][j - dy] ?? -1);
      for (let q = 0; q < 16; q++)
        if (this.seen(rows[-NY[q]], s.kind, s.index + NX[q]) === NEW)
          this.pattern |= 1 << q;
      this.shapes.forEach(([rx, ry], i) => {
        let h = mix(100 + i, s.kind);
        for (let dy = -ry; dy <= 0; dy++)
          for (let dx = -rx; dx <= rx; dx++)
            if (dy < 0 || dx < 0)
              h = mix(h, this.seen(rows[-dy], s.kind, s.index + dx));
        c[BASE + i] = h;
      });
    } else
      this.shapes.forEach((_, i) => c[BASE + i] = mix(200 + i, s.kind));
    this.kind = s.kind;
  }
  /** Codes one decision: tag 0 asks "△?", 1 "written earlier?", 2 bit k of a position whose higher bits are v. */
  decide(code, tag, k, v, bit) {
    const ti = tag < 2 ? tag : 2 + Math.min(k, 29);
    const tc = tag < 2 ? tag + 1 : mix(mix(3, k), v);
    const base = ti * KINDS + this.kind;
    const slots = [], x = [];
    for (let i = 0; i < this.contexts; i++) {
      const h = fin(mix(mix(this.ctx[i], tc), i)), ix = h & (1 << TABLE_BITS) - 1, ck = h >>> 24;
      if (this.check[ix] !== ck) {
        this.check[ix] = ck;
        this.fast[ix] = 0;
        this.fastN[ix] = 0;
        this.slow[ix] = 32768;
        this.slowN[ix] = 0;
      }
      slots.push(ix);
      x.push(this.fast[ix], this.slowN[ix] ? STRETCH[this.slow[ix]] * this.slowScale >> 8 : 0);
    }
    x.push(256);
    const set1 = this.grid && tag === 0 && this.kind === H ? SETS + this.pattern : base;
    const set2 = base * 16 + (this.grid ? this.pattern & 15 : 0);
    const seta = base * 4096 + (this.grid ? this.pattern & 4095 : 0);
    const d1 = this.mix(this.w1, this.w1Set, set1, x), d2 = this.mix(this.w2, this.w2Set, set2, x);
    const dm = d1 + d2 >> 1, pm = squash(dm);
    const a = seta * 25;
    if (!this.apmSet[seta]) {
      this.apmSet[seta] = 1;
      for (let j = 0; j < 25; j++)
        this.apm[a + j] = squash(j * 256 - 3072);
    }
    const pos = dm + 3072, ai = a + (pos >> 8), wf = pos & 255;
    const pa = this.apm[ai] * (256 - wf) + this.apm[ai + 1] * wf >> 8;
    let p = pm * (4 - this.apmWeight) + pa * this.apmWeight >> 2;
    p = p < 1 ? 1 : p > 65535 ? 65535 : p;
    const r = code(bit, p);
    const uses = this.w1Uses[set1];
    if (uses < 65535)
      this.w1Uses[set1]++;
    const rate = Math.max(Math.floor(this.rate * this.decay / (this.decay + uses)), this.minRate);
    this.learn(this.w1, set1, x, (r << 16) - squash(d1), rate);
    this.learn(this.w2, set2, x, (r << 16) - squash(d2), 65536);
    this.apm[ai] += ((r << 16) - this.apm[ai]) * (256 - wf) >> 8 + this.apmRate;
    this.apm[ai + 1] += ((r << 16) - this.apm[ai + 1]) * wf >> 8 + this.apmRate;
    for (const ix of slots) {
      this.fast[ix] += (r ? this.fastMax : -this.fastMax) - this.fast[ix] >> this.fastN[ix];
      if (this.fastN[ix] < 2)
        this.fastN[ix]++;
      this.slow[ix] += Math.floor(((r << 16) - this.slow[ix]) * RATE[this.slowN[ix]] / 65536);
      if (this.slowN[ix] < this.slowLimit)
        this.slowN[ix]++;
    }
    return r;
  }
  mix(w, initialized, set, x) {
    const o = set * this.inputs;
    if (!initialized[set]) {
      initialized[set] = 1;
      for (let i = 0; i < this.inputs; i++)
        w[o + i] = i === this.inputs - 1 ? 0 : i & 1 ? this.w0slow : this.w0;
    }
    let dot = 0;
    for (let i = 0; i < this.inputs; i++)
      dot += w[o + i] * x[i];
    return clamp(Math.floor(dot / 65536));
  }
  learn(w, set, x, err, rate) {
    const o = set * this.inputs, round = 2 ** (this.learnShift - 1), scale = 2 ** this.learnShift;
    for (let i = 0; i < this.inputs; i++)
      w[o + i] += Math.floor((Math.trunc(x[i] * err * rate / 65536) + round) / scale);
  }
};
var Decoder = class {
  constructor(bytes) {
    this.bytes = bytes;
    this.range = 4294967295;
    this.x = 0;
    this.pos = 0;
    for (let i = 0; i < 4; i++)
      this.x = this.x * 256 + this.byte();
  }
  byte() {
    return this.pos < this.bytes.length ? this.bytes[this.pos++] : 0;
  }
  code(_, p) {
    const bound = Math.floor(this.range * p / 65536);
    const bit = this.x < bound ? 1 : 0;
    if (bit)
      this.range = bound;
    else {
      this.x -= bound;
      this.range -= bound;
    }
    while (this.range < 16777216) {
      this.range = this.range * 256;
      this.x = this.x * 256 + this.byte();
    }
    return bit;
  }
};
function walk(code, known, params) {
  const model = new Model(header(code, params));
  const nodes = [[0, 0]];
  const token = [0], args = [0];
  const written = /* @__PURE__ */ new Map();
  const head = (s) => s.kind === T ? { ...s, list: s.list, index: s.index, kind: H } : { ...s, list: model.intern(s.list, s.kind, s.index), index: 0, kind: H };
  const tail = (s) => s.kind === T ? { ...s, list: s.list, index: s.index + 1, kind: T } : { ...s, list: model.intern(s.list, s.kind, s.index), index: 1, kind: T };
  const stack = [];
  let result = 0;
  const start = (x, s, fork) => {
    model.enter(s);
    if (model.decide(code, 0, 0, 0, x === 0 ? 1 : 0)) {
      model.record(s, LEAF);
      result = 0;
      return;
    }
    const n = nodes.length - 1;
    if (model.decide(code, 1, 0, 0, x !== null && written.has(x) ? 1 : 0)) {
      const i = x === null ? 0 : written.get(x) - 1;
      const width = n > 1 ? (n - 1).toString(2).length : 1;
      let v = 0;
      for (let k = width - 1; k >= 0; k--)
        v = v * 2 + model.decide(code, 2, k, v, i >> k & 1);
      if (v >= n)
        raise("dagz: reference to a node not yet written");
      model.record(s, REF);
      token[v + 1] = v + 1;
      args[v + 1] = 0;
      result = v + 1;
      return;
    }
    model.record(s, NEW);
    stack.push({ x, s, fork, left: -1 });
    const l = x === null ? null : known.nodes[x][0];
    start(l === null ? null : l, { ...s, kind: X, side: 0, spine: s.side === 0 ? s.spine + 1 : 1, path: s.path << 1 & 15, plen: Math.min(s.plen + 1, 4) }, s);
  };
  start(known && known.root, { side: 1, spine: 0, path: 0, plen: 0, h0: 0, h1: 0, list: 0, index: 0, kind: T }, null);
  while (stack.length) {
    const f = stack[stack.length - 1];
    if (f.left < 0) {
      f.left = result;
      const li = result, s = f.s;
      let r = f.fork && li === 0 ? head(f.fork) : li > 0 && nodes[li][0] === 0 ? tail(s) : { ...s, kind: X };
      r = { ...r, side: 1, spine: 0, path: (s.path << 1 | 1) & 15, plen: Math.min(s.plen + 1, 4), h0: mix(token[li], args[li]), h1: s.h0 };
      const rx = f.x === null ? null : known.nodes[f.x][1];
      start(rx, r, null);
    } else {
      stack.pop();
      nodes.push([f.left, result]);
      const id2 = nodes.length - 1;
      token.push(token[f.left]);
      args.push(args[f.left] + 1);
      if (f.x !== null)
        written.set(f.x, id2);
      result = id2;
    }
  }
  return { nodes, root: result };
}
function decode(bytes) {
  const decoder = new Decoder(bytes);
  return walk((bit, p) => decoder.code(bit, p), null);
}
function build(bytes, leaf, apply) {
  const { nodes, root } = decode(bytes);
  const built = [leaf];
  for (let i = 1; i < nodes.length; i++)
    built.push(apply(built[nodes[i][0]], built[nodes[i][1]]));
  return built[root];
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
    to: (_) => raise("dagz is read-only here: its encoder takes a DAG, which says what is shared, not a tree")
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
      const [value2, format] = formatters_infer[current_format](content);
      last_format = format;
      current_value = lazy_stacks_default.apply(current_value, value2);
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
