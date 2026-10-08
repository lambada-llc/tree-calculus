// The lambada compiler (a TC program) compiling a source file: how much of its run is `fix`
// unfolding, i.e. what the order-1 fix rule `apply(fix f, x) ⟶ reduce f (fix f) [applyTo x]` saves.
import * as T from './tc.mjs';
import * as S from './sym.mjs';
import { readFileSync } from 'node:fs';
const env = T.loadBundle('/home/user/lambada/compiler/compile_file.dag');
const fix = env.get('fix'), compile = env.get('Lambada.compile_file');
const d1 = S.derive(S.reduce(S.lit(fix), S.svar(0)));
const P = d1.s.r;
const rule = S.derive(S.reduce(P, S.svar(1)));
console.log('compiler fix rule:', S.showTree(rule, (i) => ['f', 'x'][i]).trim());
const src = readFileSync(process.argv[2] || '/home/user/arboretum/src/core.lamb', 'utf8');
const str = [...Buffer.from(src, 'utf8')].reduceRight((t, c) => T.fork(T.ofNat(c), t), T.LEAF);
function match(P, x, sig) {
  switch (P.t) {
    case 'lit': return x === P.v;
    case 'var': sig[P.i] = x; return true;
    case 'stem': return T.isStem(x) && match(P.u, T.L[x], sig);
    case 'fork': return T.isFork(x) && match(P.u, T.L[x], sig) && match(P.v, T.R[x], sig);
  }
}
const isFix = new Map(); let fixApps = 0; const fs = new Set();
const st = T.makeStats();
const t0 = Date.now();
const out = T.apply(compile, str, { onReduce: (a) => {
  let m = isFix.get(a);
  if (m === undefined) { const sig = []; m = match(P, a, sig); isFix.set(a, m); if (m) fs.add(sig[0]); }
  if (m) fixApps++;
} }, st);
const chars = []; let o = out; while (T.isFork(o)) { chars.push(T.toNat(T.L[o])); o = T.R[o]; }
console.log(`source ${src.length} bytes -> output ${chars.length} chars in ${Date.now() - t0} ms`);
console.log(`steps ${st.steps}, transitions ${st.transitions}; fix applications ${fixApps} (distinct fix f: ${fs.size}); ` +
  `steps inside fix unfoldings ${fixApps * rule.steps} = ${(100 * fixApps * rule.steps / st.steps).toFixed(1)}%`);
// with the fix jet: same answer, fewer steps
const st2 = T.makeStats();
const out2 = T.apply(compile, str, { jet: (a, b, stack, s) => {
  if (!isFix.get(a)) return null;
  const sig = []; match(P, a, sig);
  stack.push([T.APPLY_TO, b]); s.jetSaved += rule.steps - 1;
  return { kind: 'reduce', a: sig[0], b: a };
} }, st2);
console.log(`with the fix jet: same output ${out2 === out}, steps ${st2.steps} (${(st.steps / st2.steps).toFixed(3)}x), transitions ${st2.transitions} (${(st.transitions / st2.transitions).toFixed(3)}x)`);
process.stdout.write(String.fromCharCode(...chars.slice(0, 200)) + '\n');
