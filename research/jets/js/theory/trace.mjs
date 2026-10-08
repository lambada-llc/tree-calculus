// Print the symbolic run of a state step by step with the rule each transition uses.
import * as S from './sym.mjs';
export function trace(s, names, max = 100) {
  const hist = {};
  for (let n = 0; n < max; n++) {
    const o = S.sstep(s);
    console.log(String(n).padStart(3), S.showState(s, names), o.t === 'next' ? `--${o.rule}-->` : `[${o.t}${o.i !== undefined ? ' ' + names(o.i) + ' ' + o.where : ''}]`);
    if (o.t !== 'next') break;
    hist[o.rule] = (hist[o.rule] || 0) + 1;
    s = o.s;
  }
  console.log('rules:', JSON.stringify(hist));
  return hist;
}
