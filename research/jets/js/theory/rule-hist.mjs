import * as T from './tc.mjs';
import { readFileSync } from 'node:fs';
const env = T.loadBundle('/home/user/lambada/compiler/compile_file.dag');
const compile = env.get('Lambada.compile_file');
const src = readFileSync('/home/user/arboretum/src/core.lamb', 'utf8');
const str = [...Buffer.from(src, 'utf8')].reduceRight((t, c) => T.fork(T.ofNat(c), t), T.LEAF);
const h = { '0a': 0, '0b': 0, '1': 0, '2': 0, '3a': 0, '3b': 0, '3c': 0, kAbsorberFrame: 0 };
T.apply(compile, str, { onReduce: (a, b, stack) => {
  if (T.isLeaf(a)) h['0a']++; else if (T.isStem(a)) h['0b']++;
  else { const u = T.L[a]; if (T.isLeaf(u)) h['1']++; else if (T.isStem(u)) { h['2']++; if (T.isFork(T.L[u]) && T.isLeaf(T.L[T.L[u]])) h.kAbsorberFrame++; }
    else { if (T.isLeaf(b)) h['3a']++; else if (T.isStem(b)) h['3b']++; else h['3c']++; } }
} });
const tot = Object.entries(h).filter(([k]) => k !== 'kAbsorberFrame').reduce((s, [, v]) => s + v, 0);
for (const [k, v] of Object.entries(h)) console.log(k.padEnd(15), String(v).padStart(8), (100 * v / tot).toFixed(1) + '%');
