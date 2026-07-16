#pragma once

#include <vector>
#include <functional>
#include <string>

// Eager evaluator using a flat buffer with pointer sharing, constant-size
// tagless nodes, and an explicit VM-style evaluation loop — apply() uses no
// recursion, managing its entire state via an explicit continuation stack.
//
// Tree representation is the same as EagerTernaryNil:
//   <0> <0>      — leaf
//   <child> <0>  — stem
//   <a> <b>      — fork  (both non-null)
// Position 0 is reserved (0 is the null child sentinel); the shared leaf
// lives at position 1.

class EagerTernaryNilVM {
private:
  std::vector<size_t> _buf;

  // Stack frame types for the VM.
  //
  //   APPLY_TO(arg):
  //     When the current computation produces result r,
  //     begin computing apply(r, arg).
  //
  //   COMPUTE_AND_APPLY(fn, arg):
  //     When the current computation produces result r,
  //     push APPLY_TO(r) and begin computing apply(fn, arg).
  //     This supports the pattern  apply(apply(fn, arg), r)
  //     used in the fork-stem reduction rule.
  enum FrameTag { APPLY_TO, COMPUTE_AND_APPLY };

  struct Frame {
    FrameTag tag;
    size_t arg1;
    size_t arg2 = 0; // only used for COMPUTE_AND_APPLY
  };

public:
  using Tree = size_t;

  EagerTernaryNilVM() {
    _buf.push_back(0);  // position 0 reserved: 0 is the null child sentinel
    _buf.push_back(0);  // pre-populate leaf at position 1
    _buf.push_back(0);
  }

  std::string stats() {
    return std::to_string(_buf.size()) + " entries in buffer";
  }

  Tree leaf() {
    return 1;
  }

  Tree stem(Tree u) {
    size_t result = _buf.size();
    _buf.push_back(u);
    _buf.push_back(0);
    return result;
  }

  Tree fork(Tree u, Tree v) {
    size_t result = _buf.size();
    _buf.push_back(u);
    _buf.push_back(v);
    return result;
  }

  // Callables are template parameters (not std::function) so Evaluator's triage
  // uses (parse/print) inline; this VM keeps its own iterative apply() below.
  template <typename FL, typename FS, typename FF>
  [[gnu::always_inline]] auto triage(FL leaf_case, FS stem_case, FF fork_case, Tree x)
      -> decltype(leaf_case())
  {
    Tree c1 = _buf[x];
    Tree c2 = _buf[x + 1];
    if (c1 == 0) return leaf_case();
    if (c2 == 0) return stem_case(c1);
    return fork_case(c1, c2);
  }

  Tree apply(Tree a, Tree b) {
    std::vector<Frame> stack;
    Tree result;

  reduce: // ---- evaluate apply(a, b) ----
    {
      Tree u = _buf[a];
      Tree y = _buf[a + 1];

      if (u == 0) {                                      // apply(△, b) = △b
        result = stem(b);
        goto dispatch;
      }

      if (y == 0) {                                      // apply(△u, b) = △ub
        result = fork(u, b);
        goto dispatch;
      }

      Tree w = _buf[u];
      Tree x = _buf[u + 1];

      if (w == 0) {                                      // apply(△△y, b) = y
        result = y;
        goto dispatch;
      }

      if (x == 0) {                                      // apply(△(△u')y, b) = apply(apply(u', b), apply(y, b))
        stack.push_back({COMPUTE_AND_APPLY, w, b});
        a = y;
        goto reduce;
      }

      // apply(△(△wx)y, b) — triage on b
      Tree d = _buf[b];
      Tree e = _buf[b + 1];

      if (d == 0) {                                      //   b = △:     w
        result = w;
        goto dispatch;
      }
      if (e == 0) {                                      //   b = △d:    apply(x, d)
        a = x;
        b = d;
        goto reduce;
      }
      stack.push_back({APPLY_TO, e});                    //   b = △de:   apply(apply(y, d), e)
      a = y;
      b = d;
      goto reduce;
    }

  dispatch: // ---- dispatch result through stack ----
    if (stack.empty()) return result;
    Frame f = stack.back(); stack.pop_back();
    if (f.tag == APPLY_TO) {
      a = result;
      b = f.arg1;
    } else { // COMPUTE_AND_APPLY: apply(apply(fn, arg), result)
      stack.push_back({APPLY_TO, result});
      a = f.arg1;
      b = f.arg2;
    }
    goto reduce;
  }
};
