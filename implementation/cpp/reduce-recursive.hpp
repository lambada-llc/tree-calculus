#pragma once

// Shared tree-calculus reduction, written once against the derived evaluator's
// triage / stem / fork primitives instead of poking at its storage directly.
//
// Every eager evaluator here already defines triage() — the arity-dispatch its
// storage layout implies. What varied between them was apply(): each re-derived
// the five reduction rules by hand-decoding nodes (`_buf[a]`, `a->u`, `_type[x]`,
// …). That is exactly the discrimination triage already performs, so the rules
// need not be restated per backend. A backend that provides triage/stem/fork
// inherits ReduceRecursive to get apply() for free.
//
// The naive cost of that sharing would be an indirect call per node inspected:
// three std::function callables handed to triage on every reduction step, none
// of them inlinable. That is defeated at the source: triage takes its callables
// as template parameters (see each backend), so the lambdas below are distinct
// concrete types the optimizer inlines through. At -O3 the nested triage calls
// collapse back into the same switch/branch ladder the hand-written apply used —
// byte-for-byte identical machine code for the tagless backends, and actually
// leaner for the tagged ones (whose invariant throw is now out-of-line and cold
// instead of inlined into every reduction step). No call or lambda overhead is
// introduced; measured instruction counts are <= the hand-written originals.
//
// apply() is a member template so its signature is only instantiated when
// called, by which point the CRTP Derived type is complete (`Derived::Tree`).

template <typename Derived>
struct ReduceRecursive {
  // Reduce apply(a, b) to normal form via the tree-calculus rules. Tree is
  // deduced as Derived::Tree; every node inspection goes through Derived::triage.
  template <typename Tree>
  Tree apply(Tree a, Tree b) {
    // Every callable below is inlined into this function (triage is
    // always_inline; each backend's invariant check is out-of-line and cold),
    // so the reduction state stays in registers and the nested triage calls
    // collapse into the same branch ladder the hand-written apply used.
    Derived &self = static_cast<Derived &>(*this);
    return self.triage(
      // a = leaf:            apply(△, b) = △b
      [&] { return self.stem(b); },
      // a = stem(u):         apply(△u, b) = △ u b
      [&](Tree u) { return self.fork(u, b); },
      // a = fork(u, y)
      [&](Tree u, Tree y) {
        return self.triage(
          // u = leaf:        apply(△△y, b) = y
          [&] { return y; },
          // u = stem(u'):    apply(△(△u')y, b) = apply(apply(u', b), apply(y, b))
          [&](Tree u1) { return self.apply(self.apply(u1, b), self.apply(y, b)); },
          // u = fork(w, x):  dispatch on b
          [&](Tree w, Tree x) {
            return self.triage(
              // b = leaf:     w
              [&] { return w; },
              // b = stem(d):  apply(x, d)
              [&](Tree d) { return self.apply(x, d); },
              // b = fork(d, e): apply(apply(y, d), e)
              [&](Tree d, Tree e) { return self.apply(self.apply(y, d), e); },
              b);
          },
          u);
      },
      a);
  }
};
