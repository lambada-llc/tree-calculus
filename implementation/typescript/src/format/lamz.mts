// lamz: a lambda-calculus module, compressed, followed by raw bytes.
//
// A module is a list of terms. Each may refer to the ones before it, and the
// last is the program, which the file's remaining bytes are handed to as a
// list of bits (most significant bit of each byte first; △ is 0, △ △ is 1).
// The tree a file denotes is that application, with every λ removed by plain
// bracket abstraction (`bracket_ski`), so evaluation is call by value and a
// λ body is not reduced before the λ is applied.
//
// Terms are written in preorder, one binary decision at a time, each coded by
// an arithmetic coder under a prediction that mixes several adaptive contexts
// (the node's parent and grandparent, the last two nodes written, and for an
// argument the head of the application it is passed to and its position
// there). Everything is integer arithmetic, so encoder and decoder agree bit
// for bit on any platform.

import { Evaluator, raise } from "../common.mjs";
import { abs, app, node, Term_Lambda, variable } from "../abstraction-elimination/term.mjs";

/** A term of a module: `G` refers to an earlier term by position, `V` to an enclosing λ by de Bruijn index. */
export type Term =
  { t: 'A', f: Term, x: Term } |
  { t: 'L', body: Term } |
  { t: 'V', i: number } |
  { t: 'G', j: number } |
  { t: 'C' };

// ─── Probabilities ────────────────────────────────────────────────────────
// 12-bit probabilities; stretch(p) = ln(p/(1-p)) and squash its inverse, both
// scaled by 256 and interpolated from a table so that no floating point is
// involved.

const SQUASH_TABLE = [1, 2, 3, 6, 10, 16, 27, 45, 73, 120, 194, 310, 488, 747, 1101, 1546, 2047,
  2549, 2994, 3348, 3607, 3785, 3901, 3975, 4022, 4050, 4068, 4079, 4085, 4089, 4092, 4093, 4094];
function squash(d: number): number {
  if (d > 2047) return 4095;
  if (d < -2047) return 1;
  const w = d & 127, i = (d >> 7) + 16;
  return (SQUASH_TABLE[i] * (128 - w) + SQUASH_TABLE[i + 1] * w + 64) >> 7;
}
const STRETCH = new Int16Array(4096);
{
  let pi = 0;
  for (let x = -2047; x <= 2047; x++) {
    const v = squash(x);
    for (let j = pi; j <= v; j++) STRETCH[j] = x;
    pi = v + 1;
  }
  for (let j = pi; j < 4096; j++) STRETCH[j] = 2047;
}

// A context's probability moves 1/(n + 1.5) of the way to each bit it sees,
// for the first LIMIT bits, and by 1/(LIMIT + 1.5) after.
const LIMIT = 20;
const RATE = Array.from({ length: LIMIT + 1 }, (_, n) => Math.floor(131072 / (2 * n + 3)));

class Model {
  private contexts = new Map<string, number[]>();
  private weights = new Map<string, number[]>();
  private used: number[][] = [];
  private inputs: number[] = [];
  private mixer: number[] = [];
  private p = 2048;

  predict(contexts: string[], selector: string): number {
    this.used = contexts.map(c => {
      let e = this.contexts.get(c);
      if (!e) this.contexts.set(c, e = [32768, 0]);
      return e;
    });
    this.inputs = this.used.map(e => STRETCH[e[0] >> 4]);
    let w = this.weights.get(selector);
    if (!w) this.weights.set(selector, w = contexts.map(() => 1 << 13));
    this.mixer = w;
    let dot = 0;
    for (let i = 0; i < w.length; i++) dot += w[i] * this.inputs[i];
    // dot can pass 2^31, so no `>>` on it.
    this.p = Math.min(Math.max(squash(Math.floor(dot / 65536)), 1), 4095);
    return this.p;
  }

  update(bit: number): void {
    const err = (bit << 12) - this.p;
    for (let i = 0; i < this.mixer.length; i++) this.mixer[i] += (this.inputs[i] * err) >> 10;
    for (const e of this.used) {
      e[0] += Math.floor(((bit << 16) - e[0]) * RATE[e[1]] / 65536);
      if (e[1] < LIMIT) e[1]++;
    }
  }
}

// ─── Arithmetic coding ────────────────────────────────────────────────────
// A 32-bit carryless binary coder. The encoder ends on four bytes of its low
// bound, which is exactly as far as the decoder has read when it decodes the
// last decision, so the raw bytes after the module start right there.

class Encoder {
  private x1 = 0;
  private x2 = 0xffffffff;
  readonly out: number[] = [];
  code(bit: number, p: number): number {
    const xm = this.x1 + ((this.x2 - this.x1) >>> 12) * p;
    if (bit) this.x2 = xm; else this.x1 = xm + 1;
    while (((this.x1 ^ this.x2) & 0xff000000) === 0) {
      this.out.push(this.x2 >>> 24);
      this.x1 = (this.x1 << 8) >>> 0;
      this.x2 = ((this.x2 << 8) | 255) >>> 0;
    }
    return bit;
  }
  flush(): void {
    for (let i = 0; i < 4; i++) {
      this.out.push(this.x1 >>> 24);
      this.x1 = (this.x1 << 8) >>> 0;
    }
  }
}

class Decoder {
  private x1 = 0;
  private x2 = 0xffffffff;
  private x = 0;
  pos = 0;
  constructor(private bytes: Uint8Array) {
    for (let i = 0; i < 4; i++) this.x = ((this.x << 8) | this.byte()) >>> 0;
  }
  private byte(): number { return this.pos < this.bytes.length ? this.bytes[this.pos++] : (this.pos++, 0); }
  code(_: number, p: number): number {
    const xm = this.x1 + ((this.x2 - this.x1) >>> 12) * p;
    const bit = this.x <= xm ? 1 : 0;
    if (bit) this.x2 = xm; else this.x1 = xm + 1;
    while (((this.x1 ^ this.x2) & 0xff000000) === 0) {
      this.x1 = (this.x1 << 8) >>> 0;
      this.x2 = ((this.x2 << 8) | 255) >>> 0;
      this.x = ((this.x << 8) | this.byte()) >>> 0;
    }
    return bit;
  }
}

// ─── The walk ─────────────────────────────────────────────────────────────
// One walk serves both directions: encoding, `known` holds the term and each
// decision is taken from it; decoding, it is null and the decisions come back
// from the coder. Either way the terms are rebuilt from the decisions, so the
// two directions cannot drift apart.

type Code = (bit: number, p: number) => number;

function walk(code: Code, known: Term[] | null): Term[] {
  const model = new Model();
  const decide = (name: string, contexts: string[], selector: string, bit: number): number => {
    const p = model.predict(contexts.map(c => `${name};${c}`), `${name};${selector}`);
    const result = code(bit, p);
    model.update(result);
    return result;
  };
  const history = ['', ''];

  const term = (t: Term | null, par: string, gpar: string, depth: number, head: string, defined: number): Term => {
    const h1 = history[history.length - 1], h = `${history[history.length - 2]},${h1}`;
    const c = [par, par + gpar, head, head + par, par + h, par + gpar + h, head + h1];
    const kind = t === null ? 0 : { A: 0, L: 1, V: 2, G: 2, C: 3 }[t.t];
    const b0 = decide('a', c, par, kind >> 1);
    const kind_ = b0 * 2 + decide(`b${b0}`, c, par, kind & 1);
    history.push(String(kind_));
    switch (kind_) {
      case 0: {
        const a = t as { f: Term, x: Term } | null;
        const f = term(a && a.f, 'f', par, depth, `F${head}`, defined);
        let hd = f, n = 1;
        while (hd.t === 'A') hd = hd.f, n++;
        const key = hd.t === 'V' ? `V${hd.i}` : hd.t === 'G' ? `G${hd.j}` : hd.t;
        const x = term(a && a.x, 'x', par, depth, `X${key}.${n}`, defined);
        return { t: 'A', f, x };
      }
      case 1:
        return { t: 'L', body: term(t && (t as { body: Term }).body, 'l', par, depth + 1, 'L', defined) };
      case 3:
        return { t: 'C' };
    }
    const global = depth === 0 ? 1 :
      decide('g', ['', par, String(Math.min(depth, 6)), head, head + par, h, par + gpar], '', t !== null && t.t === 'G' ? 1 : 0);
    if (global) {
      const width = defined > 1 ? (defined - 1).toString(2).length : 1;
      const j = t !== null && t.t === 'G' ? t.j : 0;
      let prefix = 1;
      for (let k = width - 1; k >= 0; k--) {
        const q = `${prefix}.`;
        prefix = prefix * 2 + decide('gi',
          [q, q + par, q + Math.min(depth, 4), q + head, q + h1, q + gpar + par, q + head + par],
          String(width - k), (j >> k) & 1);
      }
      const g = prefix - (1 << width);
      if (g >= defined) raise('lamz: reference to a term not yet defined');
      history.push(`G${g}`);
      return { t: 'G', j: g };
    }
    const i = t !== null && t.t === 'V' ? t.i : -1;
    let j = 0;
    for (; j < depth - 1; j++) {
      const q = `${j}.`;
      if (decide('v', [q, q + par, q + Math.min(depth, 8), q + head, q + h1, q + depth, `${q}${head}.${depth}`],
        String(Math.min(j, 3)), j === i ? 1 : 0)) break;
    }
    history.push(`V${j}`);
    return { t: 'V', i: j };
  };

  const terms: Term[] = [];
  for (let k = 0; ; k++) {
    const last = decide('e', ['', String(Math.min(k, 1)), '', '', '', '', ''], '', known !== null && k === known.length - 1 ? 1 : 0);
    terms.push(term(known && known[k], 't', '', 0, 'T', k));
    if (last) return terms;
  }
}

export function encode(module: Term[], tail: Uint8Array = new Uint8Array()): Uint8Array {
  if (!module.length) raise('lamz: a module needs at least one term');
  const encoder = new Encoder();
  walk((bit, p) => encoder.code(bit, p), module);
  encoder.flush();
  return new Uint8Array([...encoder.out, ...tail]);
}

export function decode(bytes: Uint8Array): { module: Term[], tail: Uint8Array } {
  const decoder = new Decoder(bytes);
  const module = walk((bit, p) => decoder.code(bit, p), null);
  return { module, tail: bytes.slice(decoder.pos) };
}

// ─── From λ terms to trees ────────────────────────────────────────────────
// Bracket abstraction, except that a part of a body that does not mention the
// variable and is already a value is taken whole (K x) rather than taken apart
// (S (K a) (K b)). Values are △ applied to at most two values, and free names:
// building one reduces nothing, so building it before the λ is applied rather
// than after changes no result. Everything abstraction itself produces is such
// a value, which is what keeps a λ nested n deep from costing 3^n. An
// application that may reduce is still taken apart, so evaluation stays call
// by value: nothing in a λ body runs before the λ is applied.

const k_op = app(node, node);
const i_op = app(node, app(node, k_op), node);
const free_cache = new WeakMap<Term_Lambda, Set<string>>();
function free(t: Term_Lambda): Set<string> {
  let f = free_cache.get(t);
  if (f) return f;
  switch (t.variant) {
    case 'Node': f = new Set(); break;
    case 'Var': f = new Set([t.name]); break;
    case 'App': f = new Set([...free(t.a), ...free(t.b)]); break;
    case 'Abs': f = new Set([...free(t.body)].filter(n => n !== t.name)); break;
  }
  free_cache.set(t, f);
  return f;
}
const value_cache = new WeakMap<Term_Lambda, boolean>();
function is_value(t: Term_Lambda): boolean {
  let v = value_cache.get(t);
  if (v !== undefined) return v;
  let args = 0, head = t;
  while (head.variant === 'App') head = head.a, args++;
  v = t.variant === 'Var' || (head.variant === 'Node' && args <= 2 &&
    (args === 0 || is_value((t as { b: Term_Lambda }).b) && (args === 1 || is_value((t as { a: { b: Term_Lambda } }).a.b))));
  value_cache.set(t, v);
  return v;
}
function eliminate(name: string, t: Term_Lambda): Term_Lambda {
  if (t.variant === 'Var' && t.name === name) return i_op;
  if (!free(t).has(name) && is_value(t)) return app(k_op, t);
  if (t.variant !== 'App') return raise(`lamz: unexpected ${t.variant} in abstraction elimination`);
  return app(node, app(node, eliminate(name, t.a)), eliminate(name, t.b));
}
function combinators(t: Term_Lambda): Term_Lambda {
  switch (t.variant) {
    case 'App': return app(combinators(t.a), combinators(t.b));
    case 'Abs': return eliminate(t.name, combinators(t.body));
    default: return t;
  }
}

/**
 * What a file denotes: its terms with their λs removed as above,
 * each still referring to the ones before it by name (`g0`, `g1`, …), and the
 * tail's bits, which the last is applied to. Removing the λs from each term on
 * its own and only then putting a reference's term in its place is what keeps
 * an earlier term shared rather than rebuilt under every λ of a later one.
 */
export function read(bytes: Uint8Array): { terms: Term_Lambda[], bits: boolean[] } {
  const { module, tail } = decode(bytes);
  const lambda = (t: Term, depth: number): Term_Lambda => {
    switch (t.t) {
      case 'A': return app(lambda(t.f, depth), lambda(t.x, depth));
      case 'L': return abs(`l${depth}`, lambda(t.body, depth + 1));
      case 'V': return variable(`l${depth - 1 - t.i}`);
      case 'G': return variable(`g${t.j}`);
      case 'C': return node;
    }
  };
  const terms = module.map(t => combinators(lambda(t, 0)));
  const bits = Array.from({ length: tail.length * 8 }, (_, i) => ((tail[i >> 3] >> (7 - (i & 7))) & 1) === 1);
  return { terms, bits };
}

/** Builds a file's tree bottom up: `apply` for each application, `leaf` for △. */
function build<T>(bytes: Uint8Array, leaf: T, apply: (a: T, b: T) => T): T {
  const { terms, bits } = read(bytes);
  const built: T[] = [];
  const of = (t: Term_Lambda): T =>
    t.variant === 'Node' ? leaf :
    t.variant === 'App' ? apply(of(t.a), of(t.b)) :
    t.variant === 'Var' ? built[Number(t.name.slice(1))] :
    raise('lamz: unexpected λ after abstraction elimination');
  for (const t of terms) built.push(of(t));
  const one = apply(leaf, leaf);
  let list = leaf;
  for (let i = bits.length - 1; i >= 0; i--) list = apply(apply(leaf, bits[i] ? one : leaf), list);
  return apply(built[built.length - 1], list);
}

/** A file's tree. */
export function of<TTree>(e: Evaluator<TTree>, bytes: Uint8Array): TTree {
  return build(bytes, e.leaf, (a, b) => e.apply(a, b));
}

/**
 * A file's tree as a DAG that only applies, shared where it repeats, with
 * nothing reduced, so that a reduction can start from it anywhere. It names
 * the tree `main` and ends on it.
 */
export function to_dag(bytes: Uint8Array): string {
  const ids = new Map<string, string>();
  const lines: string[] = [];
  const root = build(bytes, '△', (a, b) => {
    const key = `${a} ${b}`;
    let n = ids.get(key);
    if (n === undefined) {
      ids.set(key, n = String(ids.size));
      lines.push(`${n} ${key}`);
    }
    return n;
  });
  return `${lines.join('\n')}\nmain ${root}\nmain\n`;
}
