import { Evaluator, Marshaller, raise } from "../common";

export type Term_Lambda =
  { variant: 'Node' } |
  { variant: 'App', a: Term_Lambda, b: Term_Lambda } |
  { variant: 'Var', name: string } |
  { variant: 'Abs', name: string, body: Term_Lambda };

export const node: Term_Lambda = { variant: 'Node' };
export const app = (...xs: Term_Lambda[]): Term_Lambda => {
  let result = xs[0];
  for (let i = 1; i < xs.length; ++i)
    result = { variant: 'App', a: result, b: xs[i] };
  return result;
};
export const variable = (name: string): Term_Lambda => ({ variant: 'Var', name });
export const abs = (name: string, body: Term_Lambda): Term_Lambda => ({ variant: 'Abs', name, body });

export const marshal_term = <TTree>(e: Evaluator<TTree>) => {
  const f =
    (term: Term_Lambda): TTree => {
      switch (term.variant) {
        case 'Node': return e.leaf;
        case 'App': return e.apply(f(term.a), f(term.b));
        default: return raise('unexpected ' + term.variant);
      }
    }
  return f;
};