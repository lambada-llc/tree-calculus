#pragma once

#include <vector>
#include <functional>
#include <string>
#include <stdexcept>

// Eager evaluator using a flat buffer with pointer sharing.
//
// Every tree is a position in the buffer where a tag (0, 1, or 2) lives.
// The buffer uses a simple, regular encoding:
//
//   0            — leaf  (1 slot)
//   1  <child>   — stem  (2 slots: tag + position of child tree)
//   2  <a> <b>   — fork  (3 slots: tag + positions of two child trees)
//
// Each <child>/<a>/<b> is a buffer index pointing directly to a tag (0/1/2).
// This invariant is maintained by construction: leaf/stem/fork/apply always
// return positions of tags, and children are always such positions.
//
// Consequences:
//   — No pointer-to-pointer chains, no resolve loop needed.
//   — No variable-length inline trees, no skip function needed.
//   — Tag vs child position is always determined by structural context.

class EagerTernaryRef {
private:
  std::vector<size_t> _buf;

public:
  using Tree = size_t;

  EagerTernaryRef() {
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
    switch (_buf[a]) {
      case 0:
        // apply(leaf, b) = stem(b)
        return stem(b);

      case 1: {
        // apply(stem(u), b) = fork(u, b)
        Tree u = _buf[a + 1];
        return fork(u, b);
      }

      case 2: {
        Tree u = _buf[a + 1];
        Tree y = _buf[a + 2];

        switch (_buf[u]) {
          case 0:
            // apply(fork(leaf, y), b) = y
            return y;

          case 1: {
            // apply(fork(stem(u'), y), b) = apply(apply(u', b), apply(y, b))
            Tree u_inner = _buf[u + 1];
            return apply(apply(u_inner, b), apply(y, b));
          }

          case 2: {
            // apply(fork(fork(w, x), y), b) — triage on b
            Tree w = _buf[u + 1];
            Tree x = _buf[u + 2];

            switch (_buf[b]) {
              case 0: return w;                                           // b = leaf
              case 1: return apply(x, _buf[b + 1]);                      // b = stem(d)
              case 2: return apply(apply(y, _buf[b + 1]), _buf[b + 2]);  // b = fork(d, e)
              default:
                throw std::runtime_error(
                  "invariant violation: unexpected value " + std::to_string(_buf[b]) +
                  " at index " + std::to_string(b));
            }
          }

          default:
            throw std::runtime_error(
              "invariant violation: unexpected value " + std::to_string(_buf[u]) +
              " at index " + std::to_string(u));
        }
      }

      default:
        throw std::runtime_error(
          "invariant violation: unexpected value " + std::to_string(_buf[a]) +
          " at index " + std::to_string(a));
    }
  }
};
