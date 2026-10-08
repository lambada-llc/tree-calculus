import * as T from './tc.mjs';
import * as S from './sym.mjs';
import { makeJet } from './accel.mjs';
import { readFileSync } from 'node:fs';
const env = T.loadBundle('/home/user/lambada/compiler/compile_file.dag');
const compile = env.get('Lambada.compile_file');
const src = readFileSync(process.argv[2] || '/home/user/arboretum/src/core.lamb', 'utf8');
const str = [...Buffer.from(src, 'utf8')].reduceRight((t, c) => T.fork(T.ofNat(c), t), T.LEAF);
const base = T.makeStats();
const r0 = T.apply(compile, str, {}, base);
console.log(`source ${src.length} B: plain steps ${base.steps}`);
const m = T.makeStats(); const rm = T.apply(compile, str, { memo: new Map() }, m);
console.log(`memo: steps ${m.steps} (x${(base.steps / m.steps).toFixed(2)}) ok ${rm === r0}`);
for (const [fuel, depth] of [[64, 0], [256, 0], [1024, 0], [1024, 2]]) {
  const stats = { rules: 0, residualNodes: 0, frames: 0, savedHist: {}, depthSum: 0 };
  const st = T.makeStats();
  const t0 = Date.now();
  const r = T.apply(compile, str, { jet: makeJet({ fuel, depth, stats }) }, st);
  console.log(`head rules fuel ${fuel} depth ${depth}: steps ${st.steps} (x${(base.steps / st.steps).toFixed(2)}), firings ${st.jets}, rules ${stats.rules}, ok ${r === r0}, ${Date.now() - t0} ms`);
}
