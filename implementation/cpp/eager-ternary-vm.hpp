#pragma once

#include <vector>
#include <functional>
#include <string>
#include <stdexcept>

// Eager evaluator using a flat buffer with pointer sharing and an explicit
// VM-style evaluation loop — apply() uses no recursion, managing its entire
// state via an explicit continuation stack.
//
// Tree representation is the same as EagerTernaryRef:
//   0            — leaf  (1 slot)
//   1  <child>   — stem  (2 slots: tag + position of child tree)
//   2  <a> <b>   — fork  (3 slots: tag + positions of two child trees)

class EagerTernaryVM {
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

  EagerTernaryVM() {
    _buf.push_back(0);  // pre-populate leaf at index 0
  }

  std::string stats() {
    return std::to_string(_buf.size()) + " entries in buffer";
  }

  Tree leaf() {
    return 0;
  }

  Tree stem(Tree u) {
    size_t result = _buf.size();
    _buf.push_back(1);
    _buf.push_back(u);
    return result;
  }

  Tree fork(Tree u, Tree v) {
    size_t result = _buf.size();
    _buf.push_back(2);
    _buf.push_back(u);
    _buf.push_back(v);
    return result;
  }

  template <typename T>
  T triage(std::function<T()> leaf_case,
           std::function<T(Tree)> stem_case,
           std::function<T(Tree, Tree)> fork_case,
           Tree x)
  {
    switch (_buf[x]) {
      case 0: return leaf_case();
      case 1: return stem_case(_buf[x + 1]);
      case 2: return fork_case(_buf[x + 1], _buf[x + 2]);
      default:
        throw std::runtime_error(
          "invariant violation: unexpected value " + std::to_string(_buf[x]) +
          " at index " + std::to_string(x));
    }
  }

  Tree apply(Tree a, Tree b) {
    std::vector<Frame> stack;
    Tree result;

  reduce: // ---- evaluate apply(a, b) ----
    switch (_buf[a]) {
      case 0:                                            // apply(△, b) = △b
        result = stem(b);
        goto dispatch;

      case 1:                                            // apply(△u, b) = △ub
        result = fork(_buf[a + 1], b);
        goto dispatch;

      case 2: {
        Tree u = _buf[a + 1];
        Tree y = _buf[a + 2];

        switch (_buf[u]) {
          case 0:                                        // apply(△△y, b) = y
            result = y;
            goto dispatch;

          case 1: {                                      // apply(△(△u')y, b) = apply(apply(u', b), apply(y, b))
            stack.push_back({COMPUTE_AND_APPLY, _buf[u + 1], b});
            a = y;
            goto reduce;
          }

          case 2: {                                      // apply(△(△wx)y, b) — triage on b
            switch (_buf[b]) {
              case 0:                                    //   b = △:     w
                result = _buf[u + 1];
                goto dispatch;
              case 1:                                    //   b = △d:    apply(x, d)
                a = _buf[u + 2];
                b = _buf[b + 1];
                goto reduce;
              case 2:                                    //   b = △de:   apply(apply(y, d), e)
                stack.push_back({APPLY_TO, _buf[b + 2]});
                a = y;
                b = _buf[b + 1];
                goto reduce;
              default: __builtin_unreachable();
            }
          }
          default: __builtin_unreachable();
        }
      }
      default: __builtin_unreachable();
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
