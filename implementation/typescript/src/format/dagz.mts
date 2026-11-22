// dagz: a DAG of applications over △, compressed, followed by raw bytes.
//
// A file is a DAG whose every node applies one node to another, with △ as the
// only leaf, so it can write down a tree before or after any amount of
// reduction. Its root is applied to the file's remaining bytes as a list of
// bits (most significant bit of each byte first; △ is 0, △ △ is 1), and that
// application is the tree the file denotes.
//
// The DAG is written as a preorder walk from the root. Each child is △, a node
// written earlier (by the position it was written at), or a new node, whose
// children follow. Every choice is made one bit at a time, each arithmetic
// coded under a prediction that mixes adaptive contexts: the side of its
// parent the child is on, how far down a chain of left children it is, the
// head and argument count of the applications it is an argument of, and the
// choices just made. Everything is integer arithmetic, so encoder and decoder
// agree bit for bit on any platform.

import { Evaluator, raise } from "../common.mjs";

/** Node i (from 1) applies nodes[i][0] to nodes[i][1]; 0 is △, and nodes[0] is unused. */
export type Dag = { nodes: [number, number][], root: number };

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
// for the first LIMIT bits, and by 1/(LIMIT + 1.5) after. Each mixer also
// weighs a constant input, which starts at weight 0.
const LIMIT = 30;
const RATE = Array.from({ length: LIMIT + 1 }, (_, n) => Math.floor(131072 / (2 * n + 3)));
const WEIGHT = 13107; // 0.2 in 16.16
const BIAS = 256;     // 1 in stretch units

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
    this.inputs = [...this.used.map(e => STRETCH[e[0] >> 4]), BIAS];
    let w = this.weights.get(selector);
    if (!w) this.weights.set(selector, w = [...contexts.map(() => WEIGHT), 0]);
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
// last decision, so the raw bytes start right there.

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
// One walk serves both directions: encoding, `known` holds the DAG and each
// decision is taken from it; decoding, it is null and the decisions come back
// from the coder. Either way the DAG is rebuilt from the decisions, so the two
// directions cannot drift apart.

type Code = (bit: number, p: number) => number;

/** Where a child sits: which side, how many left children deep, the heads of
 * the applications it is an argument of (innermost first), the last four sides. */
type Slot = { side: string, spine: number, heads: string[], path: string };

function walk(code: Code, known: Dag | null): Dag {
  const model = new Model();
  const history: string[] = [];
  const decide = (tag: string, extra: string, s: Slot, bit: number): number => {
    const h = (n: number) => history.slice(-n).join();
    const contexts = [`0${s.side}`, `1${s.side}${s.spine}.${s.heads.slice(0, 1).join()}`, `2${s.path}`, `3${h(8)}`,
      `4${s.side}${s.spine}`, `5${s.side}${s.spine}.${s.heads.slice(0, 2).join()}`];
    const p = model.predict(contexts.map(c => `${tag}${extra};${c}`), tag + extra.split('.')[0]);
    const result = code(bit, p);
    model.update(result);
    return result;
  };
  const nodes: [number, number][] = [[0, 0]];
  // A node's head: the token its leftmost chain starts at, and how many
  // arguments that chain applies it to.
  const head: { token: string, args: number }[] = [{ token: 'L', args: 0 }];
  const written = new Map<number, number>(); // encoding: known node → position written at

  const child = (x: number | null, s: Slot): number => {
    if (decide('l', '', s, x === 0 ? 1 : 0)) {
      history.push('L');
      return 0;
    }
    const n = nodes.length - 1;
    if (decide('r', '', s, x !== null && written.has(x) ? 1 : 0)) {
      const i = x === null ? 0 : written.get(x)!;
      const width = n > 1 ? (n - 1).toString(2).length : 1;
      let v = 0;
      for (let k = width - 1; k >= 0; k--) v = v * 2 + decide('t', `${k}.${v}`, s, (i >> k) & 1);
      if (v >= n) raise('dagz: reference to a node not yet written');
      history.push(`R${v}`);
      head[v + 1] = { token: `R${v}`, args: 0 };
      return v + 1;
    }
    history.push('N');
    const [l, r] = x === null ? [null, null] : known!.nodes[x];
    const left = child(l, { side: 'L', spine: s.side === 'L' ? s.spine + 1 : 1, heads: s.heads, path: (s.path + 'L').slice(-4) });
    const { token, args } = head[left];
    const right = child(r, { side: 'R', spine: 0, heads: [`${token}.${args}`, ...s.heads.slice(0, 3)], path: (s.path + 'R').slice(-4) });
    nodes.push([left, right]);
    head.push({ token, args: args + 1 });
    if (x !== null) written.set(x, nodes.length - 2);
    return nodes.length - 1;
  };
  const root = child(known && known.root, { side: 'R', spine: 0, heads: [], path: '' });
  return { nodes, root };
}

export function encode(dag: Dag, tail: Uint8Array = new Uint8Array()): Uint8Array {
  const encoder = new Encoder();
  walk((bit, p) => encoder.code(bit, p), dag);
  encoder.flush();
  return new Uint8Array([...encoder.out, ...tail]);
}

export function decode(bytes: Uint8Array): { dag: Dag, tail: Uint8Array } {
  const decoder = new Decoder(bytes);
  const dag = walk((bit, p) => decoder.code(bit, p), null);
  return { dag, tail: bytes.slice(decoder.pos) };
}

/** Builds a file's tree bottom up: `apply` for each application, `leaf` for △. */
function build<T>(bytes: Uint8Array, leaf: T, apply: (a: T, b: T) => T): T {
  const { dag, tail } = decode(bytes);
  const built: T[] = [leaf];
  for (let i = 1; i < dag.nodes.length; i++) built.push(apply(built[dag.nodes[i][0]], built[dag.nodes[i][1]]));
  const one = apply(leaf, leaf);
  let list = leaf;
  for (let i = tail.length * 8 - 1; i >= 0; i--)
    list = apply(apply(leaf, (tail[i >> 3] >> (7 - (i & 7))) & 1 ? one : leaf), list);
  return apply(built[dag.root], list);
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
