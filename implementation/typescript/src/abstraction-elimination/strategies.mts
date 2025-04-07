import { raise } from "../common.mjs";
import { abs, app, node, Term_Lambda, variable } from "./term.mjs";

const k_op = app(node, node);
const k1 = (u: Term_Lambda): Term_Lambda => app(k_op, u);
const s1 = (u: Term_Lambda): Term_Lambda => app(node, app(node, u));
const s2 = (u: Term_Lambda, v: Term_Lambda): Term_Lambda => app(s1(u), v);
const i_op = s2(k_op, node);

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
    case 'Var': return name === term.name ? i_op : k1(term);
    case 'Abs': return raise('unexpected abs');
  }
}
export const bracket_ski = process(elim_bracket_ski);

// alternative definition of [elim_star_ski] that makes difference to [elim_bracket_ski] clearer
function elim_star_ski_alt(name: string, term: Term_Lambda): Term_Lambda {
  switch (term.variant) {
    case 'Node': return k1(term);
    case 'App': return contains(name, term) ? s2(elim_star_ski(name, term.a), elim_star_ski(name, term.b)) : k1(term);
    case 'Var': return name === term.name ? i_op : k1(term);
    case 'Abs': return raise('unexpected abs');
  }
}

function elim_star_ski(name: string, term: Term_Lambda): Term_Lambda {
  if (!contains(name, term)) return k1(term);
  switch (term.variant) {
    case 'Node': return raise('unexpected node');
    case 'App': return s2(elim_star_ski(name, term.a), elim_star_ski(name, term.b));
    case 'Var': return i_op;
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
    case 'Var': return i_op;
    case 'Abs': return raise('unexpected abs');
  }
}
export const star_ski_eta = process(elim_star_ski_eta);

const c_op = star_ski_eta(abs('a', abs('b', abs('c', app(variable('a'), variable('c'), variable('b'))))));
const b_op = star_ski_eta(abs('a', abs('b', abs('c', app(variable('a'), app(variable('b'), variable('c')))))));
const s_op = star_ski_eta(abs('a', abs('b', abs('c', app(variable('a'), variable('c'), app(variable('b'), variable('c')))))));
const r_op = star_ski_eta(abs('a', abs('b', abs('c', app(variable('b'), variable('c'), variable('a'))))));
const t_op = star_ski_eta(abs('a', abs('b', app(variable('b'), variable('a')))));

function elim_star_skibc_op_eta(name: string, term: Term_Lambda): Term_Lambda {
  if (!contains(name, term)) return k1(term);
  switch (term.variant) {
    case 'Node': return raise('unexpected node');
    case 'App':
      if (!contains(name, term.b))
        // return c2(elim_star_skibc_op_eta(name, term.a), term.b);
        return app(c_op, elim_star_skibc_op_eta(name, term.a), term.b);
      if (!contains(name, term.a))
        if (term.b.variant === 'Var' && term.b.name === name)
          return term.a;
        else
          // return b2(term.a, elim_star_skibc_op_eta(name, term.b));
          return app(b_op, term.a, elim_star_skibc_op_eta(name, term.b));
      // return s2(elim_star_skibc_op_eta(name, term.a), elim_star_skibc_op_eta(name, term.b));
      return app(s_op, elim_star_skibc_op_eta(name, term.a), elim_star_skibc_op_eta(name, term.b));
    case 'Var': return i_op;
    case 'Abs': return raise('unexpected abs');
  }
}
export const star_skibc_op_eta = process(elim_star_skibc_op_eta);

// The following Kiselyov algorithms have been adapted from Ben Lynn's
// implementations at https://crypto.stanford.edu/~blynn/lambda/kiselyov.html

type bools = [] | [boolean, bools];
type term = { eta: bools; term: Term_Lambda };
function kiselyov_base(term: Term_Lambda, app: (d1: term, d2: term) => Term_Lambda): Term_Lambda {
  const zip = (a: bools, b: bools): bools =>
    a.length === 0 ? b : (b.length === 0 ? a : [a[0] || b[0], zip(a[1], b[1])]);
  const abs_stack: string[] = [];
  // convertBool (#) = \case
  //   N Z -> (True:[], Com "I")
  //   N (S e) -> (False:g, d) where (g, d) = rec $ N e
  //   L e -> case rec e of
  //     ([], d) -> ([], Com "K" :@ d)
  //     (False:g, d) -> (g, ([], Com "K") # (g, d))
  //     (True:g, d) -> (g, d)
  //   A e1 e2 -> (zipWithDefault False (||) g1 g2, t1 # t2) where
  //     t1@(g1, _) = rec e1
  //     t2@(g2, _) = rec e2
  //   Free s -> ([], Com s)
  //   where rec = convertBool (#)
  const convertBool = (term: Term_Lambda): term => {
    switch (term.variant) {
      case 'Var':
        let res: term = { eta: [true, []], term: i_op };
        let i = abs_stack.length - 1;
        for (; i >= 0 && abs_stack[i] !== term.name; --i)
          res = { eta: [false, res.eta], term: res.term };
        if (i < 0)
          return { eta: [], term };
        return res;
      case 'Abs':
        abs_stack.push(term.name);
        const rec = convertBool(term.body);
        if (rec.eta.length === 0)
          rec.term = { variant: 'App', a: k_op, b: rec.term };
        else {
          const head = rec.eta[0];
          rec.eta = rec.eta[1];
          if (!head)
            rec.term = app({ eta: [], term: k_op }, rec);
        }
        abs_stack.pop();
        return rec;
      case 'App':
        const t1 = convertBool(term.a);
        const t2 = convertBool(term.b);
        return { eta: zip(t1.eta, t2.eta), term: app(t1, t2) };
      case 'Node':
        return { eta: [], term };
    }
  };
  return convertBool(term).term;
}

export function kiselyov_plain(term: Term_Lambda): Term_Lambda {
  // (0 , d1) # (0 , d2) = d1 :@ d2
  // (0 , d1) # (n , d2) = (0, Com "B" :@ d1) # (n - 1, d2)
  // (n , d1) # (0 , d2) = (0, Com "R" :@ d2) # (n - 1, d1)
  // (n1, d1) # (n2, d2) = (n1 - 1, (0, Com "S") # (n1 - 1, d1)) # (n2 - 1, d2)
  const app = (d1: term, d2: term): Term_Lambda => {
    if (d1.eta.length === 0)
      if (d2.eta.length === 0)
        return { variant: 'App', a: d1.term, b: d2.term };
      else
        return app({ eta: [], term: { variant: 'App', a: b_op, b: d1.term } }, { eta: d2.eta[1], term: d2.term });
    else
      if (d2.eta.length === 0)
        return app({ eta: [], term: { variant: 'App', a: r_op, b: d2.term } }, { eta: d1.eta[1], term: d1.term });
      else
        return app({ eta: d1.eta[1], term: app({ eta: [], term: s_op }, { eta: d1.eta[1], term: d1.term }) }, { eta: d2.eta[1], term: d2.term });
  };
  const abs_stack: string[] = [];
  const zip = (a: bools, b: bools): bools =>
    a.length === 0 ? b : (b.length === 0 ? a : [a[0] || b[0], zip(a[1], b[1])]);
  const convertBool = (term: Term_Lambda): term => {
    switch (term.variant) {
      case 'Var':
        let res: term = { eta: [true, []], term: i_op };
        let i = abs_stack.length - 1;
        for (; i >= 0 && abs_stack[i] !== term.name; --i)
          res = { eta: [false, res.eta], term: app({ eta: [], term: k_op }, res) };
        if (i < 0)
          return { eta: [], term };
        return res;
      case 'Abs':
        abs_stack.push(term.name);
        const rec = convertBool(term.body);
        if (rec.eta.length === 0)
          rec.term = { variant: 'App', a: k_op, b: rec.term };
        else
          rec.eta = rec.eta[1];
        abs_stack.pop();
        return rec;
      case 'App':
        const t1 = convertBool(term.a);
        const t2 = convertBool(term.b);
        return { eta: zip(t1.eta, t2.eta), term: app(t1, t2) };
      case 'Node':
        return { eta: [], term };
    }
  };
  return convertBool(term).term;
}

export function kiselyov_kopt(term: Term_Lambda): Term_Lambda {
  // ([], d1) # ([], d2) = d1 :@ d2
  // ([], d1) # (True:g2, d2) = ([], Com "B" :@ d1) # (g2, d2)
  // ([], d1) # (False:g2, d2) = ([], d1) # (g2, d2)
  // (True:g1, d1) # ([], d2) = ([], Com "R" :@ d2) # (g1, d1)
  // (False:g1, d1) # ([], d2) = (g1, d1) # ([], d2)
  // (True:g1, d1) # (True:g2, d2) = (g1, ([], Com "S") # (g1, d1)) # (g2, d2)
  // (False:g1, d1) # (True:g2, d2) = (g1, ([], Com "B") # (g1, d1)) # (g2, d2)
  // (True:g1, d1) # (False:g2, d2) = (g1, ([], Com "C") # (g1, d1)) # (g2, d2)
  // (False:g1, d1) # (False:g2, d2) = (g1, d1) # (g2, d2)
  const app = (d1: term, d2: term): Term_Lambda => {
    if (d1.eta.length === 0)
      if (d2.eta.length === 0)
        return { variant: 'App', a: d1.term, b: d2.term };
      else
        if (d2.eta[0])
          return app({ eta: [], term: { variant: 'App', a: b_op, b: d1.term } }, { eta: d2.eta[1], term: d2.term });
        else
          return app(d1, { eta: d2.eta[1], term: d2.term });
    else
      if (d2.eta.length === 0)
        if (d1.eta[0])
          return app({ eta: [], term: { variant: 'App', a: r_op, b: d2.term } }, { eta: d1.eta[1], term: d1.term });
        else
          return app({ eta: d1.eta[1], term: d1.term }, d2);
      else
        return app(
          {
            eta: d1.eta[1],
            term:
              (d1.eta[0]
                ? (d2.eta[0]
                  ? app({ eta: [], term: s_op }, { eta: d1.eta[1], term: d1.term })
                  : app({ eta: [], term: c_op }, { eta: d1.eta[1], term: d1.term }))
                : (d2.eta[0]
                  ? app({ eta: [], term: b_op }, { eta: d1.eta[1], term: d1.term })
                  : d1.term))
          },
          { eta: d2.eta[1], term: d2.term });
  };
  return kiselyov_base(term, app);
}

// detects "△ (△ (△ △)) x" and "△ (△ △ △) △" as identity functions
function is_id(term: Term_Lambda): boolean {
  const is_node = (term: Term_Lambda) => term.variant === 'Node';
  const is_app = (term: Term_Lambda, p: (a: Term_Lambda, b: Term_Lambda) => boolean) =>
    term.variant === 'App' && p(term.a, term.b);
  const is_k = (term: Term_Lambda) => is_app(term, (a, b) => is_node(a) && is_node(b));
  return is_app(term, (ab, c) =>
    is_app(ab, (a, b) =>
      is_node(a) &&
      is_app(b, (b1, b2) =>
        (is_node(b1) && is_k(b2))
        ||
        (is_k(b1) && is_node(b2) && is_node(c))
      )
    )
  );
}

export function kiselyov_eta(term: Term_Lambda): Term_Lambda {
  const is_id2 = (t: term) => t.eta.length !== 0 && t.eta[1].length === 0 && is_id(t.term);
  //  ([], d1) # ([], d2) = d1 :@ d2
  // ([], d1) # (True:[], Com "I") = d1
  //  ([], d1) # (True:g2, d2) = ([], Com "B" :@ d1) # (g2, d2)
  //  ([], d1) # (False:g2, d2) = ([], d1) # (g2, d2)
  // (True:[], Com "I") # ([], d2) = Com "T" :@ d2
  // (True:[], Com "I") # (False:g2, d2) = ([], Com "T") # (g2, d2)
  //  (True:g1, d1) # ([], d2) = ([], Com "R" :@ d2) # (g1, d1)
  //  (True:g1, d1) # (True:g2, d2) = (g1, ([], Com "S") # (g1, d1)) # (g2, d2)
  //  (True:g1, d1) # (False:g2, d2) = (g1, ([], Com "C") # (g1, d1)) # (g2, d2)
  //  (False:g1, d1) # ([], d2) = (g1, d1) # ([], d2)
  // (False:g1, d1) # (True:[], Com "I") = d1
  //  (False:g1, d1) # (True:g2, d2) = (g1, ([], Com "B") # (g1, d1)) # (g2, d2)
  //  (False:g1, d1) # (False:g2, d2) = (g1, d1) # (g2, d2)
  const app = (d1: term, d2: term): Term_Lambda => {
    if (d1.eta.length === 0)
      if (d2.eta.length === 0)
        return { variant: 'App', a: d1.term, b: d2.term };
      else
        if (d2.eta[0])
          return is_id2(d2)
            ? d1.term
            : app({ eta: [], term: { variant: 'App', a: b_op, b: d1.term } }, { eta: d2.eta[1], term: d2.term });
        else
          return app(d1, { eta: d2.eta[1], term: d2.term });
    else
      if (d2.eta.length === 0)
        if (d1.eta[0])
          return is_id2(d1)
            ? { variant: 'App', a: t_op, b: d2.term }
            : app({ eta: [], term: { variant: 'App', a: r_op, b: d2.term } }, { eta: d1.eta[1], term: d1.term });
        else
          return app({ eta: d1.eta[1], term: d1.term }, d2);
      else
        if (is_id2(d1) && !d2.eta[0])
          return app({ eta: [], term: t_op }, { eta: d2.eta[1], term: d2.term });
        else if (is_id2(d2) && !d1.eta[0])
          return d1.term;
        else
          return app(
            {
              eta: d1.eta[1],
              term:
                (d1.eta[0]
                  ? (d2.eta[0]
                    ? app({ eta: [], term: s_op }, { eta: d1.eta[1], term: d1.term })
                    : app({ eta: [], term: c_op }, { eta: d1.eta[1], term: d1.term }))
                  : (d2.eta[0]
                    ? app({ eta: [], term: b_op }, { eta: d1.eta[1], term: d1.term })
                    : d1.term))
            },
            { eta: d2.eta[1], term: d2.term });
  };
  return kiselyov_base(term, app);
}
