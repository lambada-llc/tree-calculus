// dagz: a DAG of applications over △, compressed.
//
// A file is a DAG whose every node applies one node to another, with △ as the
// only leaf, so it can write down a tree before or after any amount of
// reduction. Its root is the tree the file denotes.
//
// The DAG is written as a preorder walk from the root. Each child is △, a node
// written earlier (by the position it was written at), or a new node, whose
// children follow. Every choice is made one bit at a time and arithmetic coded
// under a prediction that mixes adaptive contexts.
//
// Some contexts read the walk: the side of its parent a child is on, how far
// down a chain of left children it is, the head and argument count of the
// applications it is an argument of, the last few sides and choices. The others
// read data: a fork △ x y is a list cell, so a child has a position in a list,
// and a list that is an element of a list has a position too. A child at index
// k of row j is predicted from what was written at nearby indices of the rows
// before it, which is what makes a table, an image or any list of lists cheap.
// A header sets the model's parameters, so an encoder can fit them to the file.
// Everything is integer arithmetic, so encoder and decoder agree bit for bit on
// any platform.

import { Evaluator, raise } from "../common.mjs";

/** Node i (from 1) applies nodes[i][0] to nodes[i][1]; 0 is △, and nodes[0] is unused. */
export type Dag = { nodes: [number, number][], root: number };

// ─── Probabilities ────────────────────────────────────────────────────────
// 16-bit probabilities. stretch(p) = ln(p/(1-p)) and squash its inverse, both
// in units of 1/256, interpolated from a table so that no floating point is
// involved; logits are limited to ±12.

const SQUASH_TABLE = [1, 1, 1, 2, 3, 5, 8, 13, 22, 36, 60, 98, 162, 267, 439, 720, 1179, 1921, 3108, 4971,
  7812, 11955, 17625, 24743, 32768, 40793, 47911, 53581, 57724, 60565, 62428, 63615, 64357, 64816, 65097,
  65269, 65374, 65438, 65476, 65500, 65514, 65523, 65528, 65531, 65533, 65534, 65535, 65535, 65535];
const LOGIT = 3071;
function squash(d: number): number {
  if (d >= 3072) return 65535;
  if (d <= -3072) return 1;
  const i = (d + 3072) >> 7, w = (d + 3072) & 127;
  const v = (SQUASH_TABLE[i] * (128 - w) + SQUASH_TABLE[i + 1] * w + 64) >> 7;
  return v < 1 ? 1 : v > 65535 ? 65535 : v;
}
const STRETCH = new Int16Array(65536);
{
  let pi = 0;
  for (let d = -LOGIT; d <= LOGIT; d++) {
    const v = squash(d);
    for (let j = pi; j <= v; j++) STRETCH[j] = d;
    pi = v + 1;
  }
  for (let j = pi; j < 65536; j++) STRETCH[j] = LOGIT;
}
const clamp = (d: number) => d > LOGIT ? LOGIT : d < -LOGIT ? -LOGIT : d;

// 32-bit hashing of context values.
const mix = (h: number, v: number) => { h = Math.imul((h + v) | 0, 0x9E3779B1); return (h ^ (h >>> 15)) >>> 0; };
const fin = (h: number) => { h ^= h >>> 13; h = Math.imul(h, 0x85EBCA6B); return (h ^ (h >>> 16)) >>> 0; };

// ─── The model ────────────────────────────────────────────────────────────
// What a child turned out to be, as the contexts see it.
const UNKNOWN = 0, LEAF = 1, NEW = 2, REF = 3;
// Where a child sits: anywhere else (X), the head of a list cell (H), the rest of a list from some index on (T).
const X = 0, H = 1, T = 2;
const KINDS = 3, TAGS = 32, SETS = TAGS * KINDS;
const BASE = 6;      // contexts that read the walk
const TABLE_BITS = 24;
const RATE = Array.from({ length: 256 }, (_, n) => Math.floor(131072 / (2 * n + 3)));
// The sixteen nearest known neighbours (index offset, row offset), nearest first. They select
// weights: all sixteen for "△?" at a list head, the first twelve for the final adjustment, the
// first four for the second mixer.
const NX = [-1, 0, -1, 1, -2, 0, -2, 2, -1, 1, -3, 0, -2, 2, 3, -3];
const NY = [0, -1, -1, -1, 0, -2, -1, -1, -2, -2, 0, -3, -2, -2, -1, -1];

/**
 * The model's parameters, which a file carries in its header. Values that
 * scale are stored as codes k, standing for 2^(k/8).
 */
export type Params = {
  /** Grid contexts: each reads the rows from ry above down to this one, rx either side (in this row, only before). */
  shapes: [number, number][],
  fast: number,        // a context's fast state lives in [-fast, fast] and moves 1/2, 1/4, then 1/4 of the way
  weight: number,      // initial weight of a fast state, 16.16
  slowWeight: number,  // ... and of a slow one
  slowScale: number,   // a slow state's stretch enters the mixer × slowScale/256
  slowLimit: number,   // a slow state moves 1/(n + 1.5) of the way, n ≤ slowLimit
  rate: number,        // the first mixer's learning rate starts at rate·decay/(decay + n) after n uses of a set
  decay: number,
  minRate: number,     // ... but never below minRate
  learn: number,       // weights move by input × error × rate / 2^(16 + learn)
  apmRate: number,     // the final adjustment moves 1/2^apmRate of the way
  apmWeight: number,   // ... and counts apmWeight/4
};
const MANTISSA = [32768, 35734, 38968, 42495, 46341, 50535, 55109, 60097];
const value = (k: number) => Math.floor(MANTISSA[k & 7] * 2 ** (k >> 3) / 32768);
const CODED = ['fast', 'weight', 'slowWeight', 'slowScale', 'slowLimit', 'rate', 'decay', 'minRate'] as const;

/** The parameters a file without its own uses. */
const DEFAULTS: Params = {
  shapes: [[1, 1], [2, 2], [3, 3], [4, 4], [6, 6], [8, 1], [1, 8], [16, 0], [0, 16], [2, 4], [4, 2]],
  fast: 56, weight: 120, slowWeight: 104, slowScale: 40, slowLimit: 64, rate: 136, decay: 48, minRate: 120,
  learn: 11, apmRate: 6, apmWeight: 2,
};

// The header is one bit, then the parameters if that bit is set.
function header(code: (bit: number, p: number) => number, given?: Params): Params {
  const put = (v: number, bits: number) => {
    let r = 0;
    for (let i = bits - 1; i >= 0; i--) r = r * 2 + code((v >> i) & 1, 32768);
    return r;
  };
  if (!put(given ? 1 : 0, 1)) return DEFAULTS;
  const shapes: [number, number][] = [];
  const n = put(given ? given.shapes.length : 0, 5);
  for (let i = 0; i < n; i++) shapes.push([put(given ? given.shapes[i][0] : 0, 5), put(given ? given.shapes[i][1] : 0, 5)]);
  const p = { shapes } as Params;
  for (const k of CODED) p[k] = put(given ? given[k] : 0, 8);
  p.learn = put(given ? given.learn : 0, 4);
  p.apmRate = put(given ? given.apmRate : 0, 4);
  p.apmWeight = put(given ? given.apmWeight : 0, 2);
  return p;
}

type Slot = { side: number, spine: number, path: number, plen: number, h0: number, h1: number, list: number, index: number, kind: number };

class Model {
  private readonly shapes: [number, number][];
  private readonly contexts: number;
  private readonly inputs: number;
  private readonly reach: number;
  private readonly fastMax: number;
  private readonly w0: number;
  private readonly w0slow: number;
  private readonly slowScale: number;
  private readonly slowLimit: number;
  private readonly rate: number;
  private readonly decay: number;
  private readonly minRate: number;
  private readonly learnShift: number;
  private readonly apmRate: number;
  private readonly apmWeight: number;

  // Context states, direct mapped by hash with an 8-bit check.
  private check = new Uint8Array(1 << TABLE_BITS);
  private fast = new Int16Array(1 << TABLE_BITS);
  private fastN = new Uint8Array(1 << TABLE_BITS);
  private slow = new Uint16Array(1 << TABLE_BITS);
  private slowN = new Uint8Array(1 << TABLE_BITS);
  private w1: Int32Array;
  private w1Set = new Uint8Array(SETS + 65536);
  private w1Uses = new Uint16Array(SETS + 65536);
  private w2: Int32Array;
  private w2Set = new Uint8Array(SETS * 16);
  private apm = new Uint16Array(SETS * 4096 * 25);
  private apmSet = new Uint8Array(SETS * 4096);

  // Lists: each has a parent list, the index it sits at there and its kind;
  // `outcome` holds what was written at each index (per kind), `nested` the
  // lists that are elements at each index (per kind).
  private parent: number[] = [-1];
  private at: number[] = [0];
  private atKind: number[] = [0];
  private outcome: number[][][] = [[[], [], []]];
  private nested: number[][][] = [[[], [], []]];
  private history = new Int32Array(8);
  private count = 0;

  // Per slot.
  private ctx: Uint32Array;
  private pattern = 0;
  private grid = false;
  private kind = 0;

  constructor(p: Params) {
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

  intern(list: number, kind: number, i: number): number {
    const existing = this.nested[list][kind][i];
    if (existing !== undefined) return existing;
    const id = this.parent.length;
    this.parent.push(list); this.at.push(i); this.atKind.push(kind);
    this.outcome.push([[], [], []]); this.nested.push([[], [], []]);
    this.nested[list][kind][i] = id;
    return id;
  }
  private seen(list: number, kind: number, i: number): number {
    return list < 0 || i < 0 ? UNKNOWN : this.outcome[list][kind][i] ?? UNKNOWN;
  }
  record(s: Slot, o: number): void {
    if (s.kind !== X) this.outcome[s.list][s.kind][s.index] = o;
    this.history[this.count++ & 7] = o;
  }

  enter(s: Slot): void {
    let hh = 0;
    for (let i = 0; i < 8; i++) hh = mix(hh, this.history[(this.count - 1 - i) & 7] + 1);
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
      const rows: number[] = [s.list];
      for (let dy = 1; dy <= this.reach; dy++) rows.push(j - dy < 0 ? -1 : this.nested[p][pk][j - dy] ?? -1);
      for (let q = 0; q < 16; q++) if (this.seen(rows[-NY[q]], s.kind, s.index + NX[q]) === NEW) this.pattern |= 1 << q;
      this.shapes.forEach(([rx, ry], i) => {
        let h = mix(100 + i, s.kind);
        for (let dy = -ry; dy <= 0; dy++)
          for (let dx = -rx; dx <= rx; dx++)
            if (dy < 0 || dx < 0) h = mix(h, this.seen(rows[-dy], s.kind, s.index + dx));
        c[BASE + i] = h;
      });
    } else this.shapes.forEach((_, i) => c[BASE + i] = mix(200 + i, s.kind));
    this.kind = s.kind;
  }

  /** Codes one decision: tag 0 asks "△?", 1 "written earlier?", 2 bit k of a position whose higher bits are v. */
  decide(code: (bit: number, p: number) => number, tag: number, k: number, v: number, bit: number): number {
    const ti = tag < 2 ? tag : 2 + Math.min(k, 29);
    const tc = tag < 2 ? tag + 1 : mix(mix(3, k), v);
    const base = ti * KINDS + this.kind;
    const slots: number[] = [], x: number[] = [];
    for (let i = 0; i < this.contexts; i++) {
      const h = fin(mix(mix(this.ctx[i], tc), i)), ix = h & ((1 << TABLE_BITS) - 1), ck = h >>> 24;
      if (this.check[ix] !== ck) {
        this.check[ix] = ck; this.fast[ix] = 0; this.fastN[ix] = 0; this.slow[ix] = 32768; this.slowN[ix] = 0;
      }
      slots.push(ix);
      x.push(this.fast[ix], this.slowN[ix] ? (STRETCH[this.slow[ix]] * this.slowScale) >> 8 : 0);
    }
    x.push(256);
    const set1 = this.grid && tag === 0 && this.kind === H ? SETS + this.pattern : base;
    const set2 = base * 16 + (this.grid ? this.pattern & 15 : 0);
    const seta = base * 4096 + (this.grid ? this.pattern & 4095 : 0);
    const d1 = this.mix(this.w1, this.w1Set, set1, x), d2 = this.mix(this.w2, this.w2Set, set2, x);
    const dm = (d1 + d2) >> 1, pm = squash(dm);
    const a = seta * 25;
    if (!this.apmSet[seta]) { this.apmSet[seta] = 1; for (let j = 0; j < 25; j++) this.apm[a + j] = squash(j * 256 - 3072); }
    const pos = dm + 3072, ai = a + (pos >> 8), wf = pos & 255;
    const pa = (this.apm[ai] * (256 - wf) + this.apm[ai + 1] * wf) >> 8;
    let p = (pm * (4 - this.apmWeight) + pa * this.apmWeight) >> 2;
    p = p < 1 ? 1 : p > 65535 ? 65535 : p;

    const r = code(bit, p);
    const uses = this.w1Uses[set1];
    if (uses < 65535) this.w1Uses[set1]++;
    const rate = Math.max(Math.floor(this.rate * this.decay / (this.decay + uses)), this.minRate);
    this.learn(this.w1, set1, x, (r << 16) - squash(d1), rate);
    this.learn(this.w2, set2, x, (r << 16) - squash(d2), 65536);
    this.apm[ai] += (((r << 16) - this.apm[ai]) * (256 - wf)) >> (8 + this.apmRate);
    this.apm[ai + 1] += (((r << 16) - this.apm[ai + 1]) * wf) >> (8 + this.apmRate);
    for (const ix of slots) {
      this.fast[ix] += ((r ? this.fastMax : -this.fastMax) - this.fast[ix]) >> this.fastN[ix];
      if (this.fastN[ix] < 2) this.fastN[ix]++;
      this.slow[ix] += Math.floor(((r << 16) - this.slow[ix]) * RATE[this.slowN[ix]] / 65536);
      if (this.slowN[ix] < this.slowLimit) this.slowN[ix]++;
    }
    return r;
  }

  private mix(w: Int32Array, initialized: Uint8Array, set: number, x: number[]): number {
    const o = set * this.inputs;
    if (!initialized[set]) {
      initialized[set] = 1;
      for (let i = 0; i < this.inputs; i++) w[o + i] = i === this.inputs - 1 ? 0 : i & 1 ? this.w0slow : this.w0;
    }
    let dot = 0;
    for (let i = 0; i < this.inputs; i++) dot += w[o + i] * x[i];
    return clamp(Math.floor(dot / 65536));
  }
  private learn(w: Int32Array, set: number, x: number[], err: number, rate: number): void {
    const o = set * this.inputs, round = 2 ** (this.learnShift - 1), scale = 2 ** this.learnShift;
    for (let i = 0; i < this.inputs; i++) w[o + i] += Math.floor((Math.trunc(x[i] * err * rate / 65536) + round) / scale);
  }
}

// ─── Arithmetic coding ────────────────────────────────────────────────────
// A range coder with carry propagation. The encoder ends on the value in its
// final interval with the most trailing zero bits and drops the zero bytes, as
// the decoder reads zeros past the end; its first byte is always zero, so it
// is not written either.

class Encoder {
  private low = 0;
  private range = 0xffffffff;
  private cache = 0;
  private pending = 1;
  readonly out: number[] = [];
  code(bit: number, p: number): number {
    const bound = Math.floor(this.range * p / 65536);
    if (bit) this.range = bound; else { this.low += bound; this.range -= bound; }
    while (this.range < 0x1000000) { this.range = this.range * 256; this.shift(); }
    return bit;
  }
  private shift(): void {
    if (this.low < 0xff000000 || this.low >= 0x100000000) {
      const carry = this.low >= 0x100000000 ? 1 : 0;
      let t = this.cache;
      do { this.out.push((t + carry) & 255); t = 255; } while (--this.pending);
      this.cache = Math.floor(this.low / 0x1000000) & 255;
    }
    this.pending++;
    this.low = (this.low % 0x1000000) * 256;
  }
  finish(): Uint8Array {
    for (let s = 32; s >= 0; s--) {
      const m = 2 ** s, v = Math.ceil(this.low / m) * m;
      if (v < this.low + this.range) { this.low = v; break; }
    }
    for (let i = 0; i < 5; i++) this.shift();
    let n = this.out.length;
    while (n > 1 && this.out[n - 1] === 0) n--;
    return new Uint8Array(this.out.slice(1, n));
  }
}

class Decoder {
  private range = 0xffffffff;
  private x = 0;
  private pos = 0;
  constructor(private bytes: Uint8Array) {
    for (let i = 0; i < 4; i++) this.x = this.x * 256 + this.byte();
  }
  private byte(): number { return this.pos < this.bytes.length ? this.bytes[this.pos++] : 0; }
  code(_: number, p: number): number {
    const bound = Math.floor(this.range * p / 65536);
    const bit = this.x < bound ? 1 : 0;
    if (bit) this.range = bound; else { this.x -= bound; this.range -= bound; }
    while (this.range < 0x1000000) { this.range = this.range * 256; this.x = this.x * 256 + this.byte(); }
    return bit;
  }
}

// ─── The walk ─────────────────────────────────────────────────────────────
// One walk serves both directions: encoding, `known` holds the DAG and each
// decision is taken from it; decoding, it is null and the decisions come back
// from the coder. Either way the DAG is rebuilt from the decisions, so the two
// directions cannot drift apart. The walk keeps its own stack, as a list nests
// as deep as it is long.

type Code = (bit: number, p: number) => number;

function walk(code: Code, known: Dag | null, params?: Params): Dag {
  const model = new Model(header(code, params));
  const nodes: [number, number][] = [[0, 0]];
  // A node's head: the token its leftmost chain starts at, and how many
  // arguments that chain applies it to.
  const token: number[] = [0], args: number[] = [0];
  const written = new Map<number, number>(); // encoding: known node → index written as

  const head = (s: Slot): Slot => s.kind === T
    ? { ...s, list: s.list, index: s.index, kind: H }
    : { ...s, list: model.intern(s.list, s.kind, s.index), index: 0, kind: H };
  const tail = (s: Slot): Slot => s.kind === T
    ? { ...s, list: s.list, index: s.index + 1, kind: T }
    : { ...s, list: model.intern(s.list, s.kind, s.index), index: 1, kind: T };

  // A frame is a child being written: its node, its slot, the slot of the fork
  // it may be the left half of, and once its left child is done, that child.
  type Frame = { x: number | null, s: Slot, fork: Slot | null, left: number };
  const stack: Frame[] = [];
  let result = 0;
  const start = (x: number | null, s: Slot, fork: Slot | null): void => {
    model.enter(s);
    if (model.decide(code, 0, 0, 0, x === 0 ? 1 : 0)) { model.record(s, LEAF); result = 0; return; }
    const n = nodes.length - 1;
    if (model.decide(code, 1, 0, 0, x !== null && written.has(x) ? 1 : 0)) {
      const i = x === null ? 0 : written.get(x)! - 1;
      const width = n > 1 ? (n - 1).toString(2).length : 1;
      let v = 0;
      for (let k = width - 1; k >= 0; k--) v = v * 2 + model.decide(code, 2, k, v, (i >> k) & 1);
      if (v >= n) raise('dagz: reference to a node not yet written');
      model.record(s, REF);
      token[v + 1] = v + 1; args[v + 1] = 0;
      result = v + 1;
      return;
    }
    model.record(s, NEW);
    stack.push({ x, s, fork, left: -1 });
    const l = x === null ? null : known!.nodes[x][0];
    start(l === null ? null : l, { ...s, kind: X, side: 0, spine: s.side === 0 ? s.spine + 1 : 1, path: (s.path << 1) & 15, plen: Math.min(s.plen + 1, 4) }, s);
  };
  // `start` recurses only down left children, so its depth is that of a chain
  // of left children; everything else goes through the stack.
  start(known && known.root, { side: 1, spine: 0, path: 0, plen: 0, h0: 0, h1: 0, list: 0, index: 0, kind: T }, null);
  while (stack.length) {
    const f = stack[stack.length - 1];
    if (f.left < 0) {
      f.left = result;
      const li = result, s = f.s;
      let r: Slot = f.fork && li === 0 ? head(f.fork) : li > 0 && nodes[li][0] === 0 ? tail(s) : { ...s, kind: X };
      r = { ...r, side: 1, spine: 0, path: ((s.path << 1) | 1) & 15, plen: Math.min(s.plen + 1, 4), h0: mix(token[li], args[li]), h1: s.h0 };
      const rx = f.x === null ? null : known!.nodes[f.x][1];
      start(rx, r, null);
    } else {
      stack.pop();
      nodes.push([f.left, result]);
      const id = nodes.length - 1;
      token.push(token[f.left]); args.push(args[f.left] + 1);
      if (f.x !== null) written.set(f.x, id);
      result = id;
    }
  }
  return { nodes, root: result };
}

/** Encodes a DAG, with the given parameters in its header, or none and the defaults. */
export function encode(dag: Dag, params?: Params): Uint8Array {
  const encoder = new Encoder();
  walk((bit, p) => encoder.code(bit, p), dag, params);
  return encoder.finish();
}

export function decode(bytes: Uint8Array): Dag {
  const decoder = new Decoder(bytes);
  return walk((bit, p) => decoder.code(bit, p), null);
}

/** Builds a file's tree bottom up: `apply` for each application, `leaf` for △. */
function build<T>(bytes: Uint8Array, leaf: T, apply: (a: T, b: T) => T): T {
  const { nodes, root } = decode(bytes);
  const built: T[] = [leaf];
  for (let i = 1; i < nodes.length; i++) built.push(apply(built[nodes[i][0]], built[nodes[i][1]]));
  return built[root];
}

/** A file's tree. */
export function of<TTree>(e: Evaluator<TTree>, bytes: Uint8Array): TTree {
  return build(bytes, e.leaf, (a, b) => e.apply(a, b));
}

/**
 * A file's tree as a DAG that only applies, with nothing reduced, so that a
 * reduction can start from it anywhere. It names the tree `main` and ends on it.
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
