// Derive 1st-order rules for known functions applied to holes.
// usage: node derive.mjs <dag-file> <spec>...   spec = "name/arg1,arg2,..[!split]"
import fs from 'node:fs';
import * as M from './sym.mjs';

const [file, ...specs] = process.argv.slice(2);
M.loadDag(fs.readFileSync(file, 'utf8'), { prefer: (n) => !n.startsWith(':test') && !n.startsWith(':source') && !n.startsWith('_') });

function full(t) { // print without using t's own name at top level
  const saved = M.names.get(t); M.names.delete(t);
  const s = M.show(t); if (saved !== undefined) M.names.set(t, saved); return s;
}

const SHAPES = (h) => {
  const n = M.holeName.get(h);
  return [['△', () => M.LEAF], [`△${n}'`, () => M.mk(1, M.hole(n + "'"))], [`△${n}'${n}''`, () => M.mk(2, M.hole(n + "'"), M.hole(n + "''"))]];
};

function describe(label, res, lhsOpen, depth = 0, splitBudget = 0) {
  const ind = '  '.repeat(depth);
  if (res.kind === 'halt') {
    const rhsOpen = M.openNodes(res.value);
    const fresh = [...rhsOpen].filter((x) => !lhsOpen.has(x)).length;
    console.log(`${ind}${label}  ->  ${M.show(res.value)}    [steps ${res.steps}; rules L/S/K/S/FL/FS/FF ${res.rules.join('/')}; halts with value; new open nodes ${fresh}]`);
    return;
  }
  if (res.kind === 'budget') { console.log(`${ind}${label}  ->  (budget exhausted after ${res.steps})`); return; }
  const st = res.state;
  const term = M.showTerm(M.stateTerm(st));
  const vals = M.stateValues(st);
  const rhsOpen = new Set(); vals.forEach((v) => M.openNodes(v, rhsOpen));
  const fresh = [...rhsOpen].filter((x) => !lhsOpen.has(x)).length;
  const why = st.a === res.hole ? 'applies hole' : (M.K[st.a] === 2 && M.U[st.a] === res.hole ? 'needs shape of hole in function position' : 'triage on hole');
  console.log(`${ind}${label}  ->  ${term}    [steps ${res.steps}; rules L/S/K/S/FL/FS/FF ${res.rules.join('/')}; stops: ${why} ${M.holeName.get(res.hole)}; frames ${st.stack.length}; new open nodes ${fresh}]`);
  if (splitBudget > 0 && st.a !== res.hole) {
    for (const [sn, mkq] of SHAPES(res.hole)) {
      const q = mkq();
      const st2 = M.substState(st, res.hole, q);
      const r2 = M.resume(st2);
      r2.steps += res.steps; r2.rules = r2.rules.map((x, i) => x + res.rules[i]);
      describe(`${M.holeName.get(res.hole)} = ${sn}:`, r2, lhsOpen, depth + 1, splitBudget - 1);
    }
  }
}

for (const spec of specs) {
  const [nameArgs, flag] = spec.split('!');
  const [name, argstr] = nameArgs.split('/');
  const args = argstr ? argstr.split(',') : [];
  const f = M.env.get(name);
  if (f === undefined) { console.log(`?? ${name} unbound`); continue; }
  console.log(`\n== ${name}  (size ${M.SIZE[f]} nodes)  = ${full(f)}`);
  let cur = f, lhs = name;
  for (let i = 0; i < args.length; i++) {
    const h = M.hole(args[i]);
    const lhsOpen = M.openNodes(cur);
    const res = M.run(cur, h);
    const label = `${lhs} ${args[i]}`;
    const last = i === args.length - 1 || res.kind !== 'halt';
    if (res.kind === 'halt' && !last) {
      console.log(`  ${label}  =  ${M.show(res.value)}    [steps ${res.steps}; pattern open nodes ${M.openNodes(res.value).size}]`);
      cur = res.value; lhs = `(${label})`;
      continue;
    }
    describe(label, res, lhsOpen, 1, flag === 'split' ? 2 : 0);
    if (res.kind !== 'halt') break;
    cur = res.value; lhs = `(${label})`;
  }
}
