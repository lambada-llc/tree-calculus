// Speedup vs. rule length bound (fuel) and vs. problem size; with and without memo.
import * as T from './tc.mjs';
import { makeJet } from './accel.mjs';
import { readFileSync } from 'node:fs';
const B = JSON.parse(readFileSync(new URL('./bench.json', import.meta.url)));
const I = T.ofTernary('21100');
const natList = (xs) => xs.reduceRight((t, n) => T.fork(T.ofNat(n), t), T.LEAF);
const which = process.argv[2] || 'fib';
const sizes = which === 'fib' ? [10, 12, 14, 16, 18] : [25, 50, 100, 200, 400];
const prog = which === 'fib' ? B.FIB_TERNARY : B.MERGE_SORT_TERNARY;
const mk = (n) => which === 'fib' ? T.ofNat(n) : natList(Array.from({ length: n }, (_, i) => n - i));
const f = T.apply(I, T.ofTernary(prog));
function measure(x, opts) {
  const st = T.makeStats();
  const i0 = T.interns;
  const r = T.apply(f, x, opts, st);
  return { r, st, interns: T.interns - i0 };
}
console.log('which n | plain steps | memo ×steps | rules(fuel=16,64,256,1024) ×steps (max saved/firing) | rules1024+memo ×steps | work× (fuel 1024)');
for (const n of sizes) {
  const x = mk(n);
  const p = measure(x, {});
  const m = measure(x, { memo: new Map() });
  const cols = [];
  let w1024;
  for (const fuel of [16, 64, 256, 1024]) {
    const stats = { rules: 0, residualNodes: 0, frames: 0, savedHist: {}, depthSum: 0 };
    const j = measure(x, { jet: makeJet({ fuel, depth: 0, stats }) });
    if (j.r !== p.r) throw new Error('mismatch');
    const maxSaved = Math.max(...Object.keys(stats.savedHist).map(Number));
    cols.push(`${(p.st.steps / j.st.steps).toFixed(1)}(${maxSaved})`);
    if (fuel === 1024) {
      const wp = p.st.transitions + p.interns;
      const wj = j.st.transitions + j.interns + stats.residualNodes + stats.frames;
      w1024 = (wp / wj).toFixed(1);
    }
  }
  const stats = { rules: 0, residualNodes: 0, frames: 0, savedHist: {}, depthSum: 0 };
  const jm = measure(x, { memo: new Map(), jet: makeJet({ fuel: 1024, depth: 0, stats }) });
  if (jm.r !== p.r) throw new Error('mismatch2');
  console.log(`${which} ${String(n).padStart(4)} | ${String(p.st.steps).padStart(9)} | ${(p.st.steps / m.st.steps).toFixed(1).padStart(6)} | ${cols.join('  ')} | ${(p.st.steps / jm.st.steps).toFixed(1)} | ${w1024}`);
}
