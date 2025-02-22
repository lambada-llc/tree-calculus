import { raise } from "../common";
import { abs, app, node, Term_Lambda, variable } from "./term";

const k = app(node, node);
const k1 = (u: Term_Lambda): Term_Lambda => app(k, u);
const s1 = (u: Term_Lambda): Term_Lambda => app(node, app(node, u));
const s2 = (u: Term_Lambda, v: Term_Lambda): Term_Lambda => app(s1(u), v);
const i = s2(k, node);

function contains(name: string, term: Term_Lambda): boolean {
  switch (term.variant) {
    case 'Node': return false;
    case 'App': return contains(name, term.a) || contains(name, term.b);
    case 'Var': return name === term.name;
    case 'Abs': return name !== term.name && contains(name, term.body);
  }
}

const process =
  (elim: (name: string, term: Term_Lambda) => Term_Lambda) => {
    const elim_abs = (term: Term_Lambda): Term_Lambda => {
      switch (term.variant) {
        case 'Node': return term;
        case 'App': return { variant: 'App', a: elim_abs(term.a), b: elim_abs(term.b) };
        case 'Var': return term;
        case 'Abs': return elim(term.name, elim_abs(term.body));
      }
    };
    return elim_abs;
  };

function elim_bracket_ski(name: string, term: Term_Lambda): Term_Lambda {
  switch (term.variant) {
    case 'Node': return k1(term);
    case 'App': return s2(elim_bracket_ski(name, term.a), elim_bracket_ski(name, term.b));
    case 'Var': return name === term.name ? i : k1(term);
    case 'Abs': return raise('unexpected abs');
  }
}
export const bracket_ski = process(elim_bracket_ski);

// alternative definition of [elim_star_ski] that makes difference to [elim_bracket_ski] clearer
function elim_star_ski_alt(name: string, term: Term_Lambda): Term_Lambda {
  switch (term.variant) {
    case 'Node': return k1(term);
    case 'App': return contains(name, term) ? s2(elim_star_ski(name, term.a), elim_star_ski(name, term.b)) : k1(term);
    case 'Var': return name === term.name ? i : k1(term);
    case 'Abs': return raise('unexpected abs');
  }
}

function elim_star_ski(name: string, term: Term_Lambda): Term_Lambda {
  if (!contains(name, term)) return k1(term);
  switch (term.variant) {
    case 'Node': return raise('unexpected node');
    case 'App': return s2(elim_star_ski(name, term.a), elim_star_ski(name, term.b));
    case 'Var': return i;
    case 'Abs': return raise('unexpected abs');
  }
}
export const star_ski = process(elim_star_ski);

function elim_star_ski_eta(name: string, term: Term_Lambda): Term_Lambda {
  if (!contains(name, term)) return k1(term);
  switch (term.variant) {
    case 'Node': return raise('unexpected node');
    case 'App':
      if (!contains(name, term.a) && term.b.variant === 'Var' && term.b.name === name)
        return term.a;
      return s2(elim_star_ski_eta(name, term.a), elim_star_ski_eta(name, term.b));
    case 'Var': return i;
    case 'Abs': return raise('unexpected abs');
  }
}
export const star_ski_eta = process(elim_star_ski_eta);

const c2 = (u: Term_Lambda, v: Term_Lambda): Term_Lambda => s2(u, k1(v));
const b2 = (u: Term_Lambda, v: Term_Lambda): Term_Lambda => s2(k1(u), v);
const c = star_ski_eta(abs('u', abs('v', c2(variable('u'), variable('v')))));
const b = star_ski_eta(abs('u', abs('v', b2(variable('u'), variable('v')))));
const s = star_ski_eta(abs('u', s1(variable('u'))));

function elim_star_skibc_op_eta(name: string, term: Term_Lambda): Term_Lambda {
  if (!contains(name, term)) return k1(term);
  switch (term.variant) {
    case 'Node': return raise('unexpected node');
    case 'App':
      if (!contains(name, term.b))
        // return c2(elim_star_skibc_op_eta(name, term.a), term.b);
        return app(c, elim_star_skibc_op_eta(name, term.a), term.b);
      if (!contains(name, term.a))
        if (term.b.variant === 'Var' && term.b.name === name)
          return term.a;
        else
          // return b2(term.a, elim_star_skibc_op_eta(name, term.b));
          return app(b, term.a, elim_star_skibc_op_eta(name, term.b));
      // return s2(elim_star_skibc_op_eta(name, term.a), elim_star_skibc_op_eta(name, term.b));
      return app(s, elim_star_skibc_op_eta(name, term.a), elim_star_skibc_op_eta(name, term.b));
    case 'Var': return i;
    case 'Abs': return raise('unexpected abs');
  }
}
export const star_skibc_op_eta = process(elim_star_skibc_op_eta);
