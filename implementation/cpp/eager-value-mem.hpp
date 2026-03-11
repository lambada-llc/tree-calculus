#pragma once

#include <vector>
#include <functional>
#include <string>
#include <stdexcept>

class EagerValueMem {
private:
  std::vector<int> _type; // 0 = leaf, 1 = stem, 2 = fork
  std::vector<int> _u;    // (left) child for stems or forks
  std::vector<int> _v;    // right child for forks

public:
  using Tree = int;

  EagerValueMem() {
    // put the one and only leaf node at index 0
    _type.push_back(0);
    _u.push_back(0);
    _v.push_back(0);
  }

  std::string stats() {
    return std::to_string(_type.size()) + " nodes allocated";
  }

  Tree leaf() {
    return 0;
  }

  Tree stem(Tree u) {
    _type.push_back(1);
    _u.push_back(u);
    _v.push_back(0);
    return _type.size() - 1;
  }

  Tree fork(Tree u, Tree v) {
    _type.push_back(2);
    _u.push_back(u);
    _v.push_back(v);
    return _type.size() - 1;
  }

  template <typename T>
  T triage(std::function<T()> leaf_case, std::function<T(Tree)> stem_case, std::function<T(Tree, Tree)> fork_case, Tree x) {
    switch (_type[x]) {
      case 0: return leaf_case();
      case 1: return stem_case(_u[x]);
      case 2: return fork_case(_u[x], _v[x]);
      default: throw std::runtime_error("invariant violation: type " + std::to_string(_type[x]) + " at index " + std::to_string(x) + " not 0, 1 or 2");
    }
  }

  Tree apply(Tree a, Tree b) {
    switch (_type[a]) {
      case 0: return stem(b);
      case 1: return fork(_u[a], b);
      case 2:
        Tree u = _u[a];
        switch (_type[u]) {
          case 0: return _v[a];
          case 1: return apply(apply(_u[u], b), apply(_v[a], b));
          case 2:
            switch (_type[b]) {
              case 0: return _u[u];
              case 1: return apply(_v[u], _u[b]);
              case 2: return apply(apply(_v[a], _u[b]), _v[b]);
            }
            throw std::runtime_error("invariant violation: type " + std::to_string(_type[b]) + " at index " + std::to_string(b) + " not 0, 1 or 2");
        }
        throw std::runtime_error("invariant violation: type " + std::to_string(_type[u]) + " at index " + std::to_string(u) + " not 0, 1 or 2");
    }
    throw std::runtime_error("invariant violation: type " + std::to_string(_type[a]) + " at index " + std::to_string(a) + " not 0, 1 or 2");
  }

};
