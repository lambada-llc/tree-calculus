#pragma once

#include <vector>
#include <functional>
#include <string>
#include <stdexcept>

// Evaluator<Impl> adds shared tree-calculus utilities on top of a primitive
// operations class. Impl must provide:
//   using Tree = ...;
//   Tree leaf();
//   Tree stem(Tree u);
//   Tree fork(Tree u, Tree v);
//   Tree apply(Tree a, Tree b);
//   T triage(std::function<T()>, std::function<T(Tree)>, std::function<T(Tree, Tree)>, Tree);

template <typename Impl>
class Evaluator : public Impl {
public:
  using Tree = typename Impl::Tree;
  using Impl::Impl; // inherit constructors

  Tree t_false() {
    return this->leaf();
  }

  Tree t_true() {
    return this->stem(this->leaf());
  }

  bool to_bool(Tree x) {
    return this->template triage<bool>(
      []() { return false; },
      [&](Tree) { return true; },
      [&](Tree, Tree) -> bool { throw std::runtime_error("tree is not a bool"); },
      x);
  }

  Tree of_bool(bool b) {
    return b ? t_true() : t_false();
  }

  std::vector<Tree> to_list(Tree x) {
    std::vector<Tree> res;
    while (true) {
      bool done = this->template triage<bool>(
        [&]() { return true; },
        [&](Tree) -> bool { throw std::runtime_error("tree is not a list"); },
        [&](Tree u, Tree v) { res.push_back(u); x = v; return false; },
        x);
      if (done) return res;
    }
  }

  Tree of_list(std::vector<Tree> l) {
    Tree f = this->leaf();
    for (int i = l.size(); i; i--) f = this->fork(l[i - 1], f);
    return f;
  }

  int64_t to_nat(Tree x) {
    int64_t result = 0;
    std::vector<Tree> list = to_list(x);
    for (auto it = list.rbegin(); it != list.rend(); ++it)
      result = 2 * result + (to_bool(*it) ? 1 : 0);
    return result;
  }

  Tree of_nat(int64_t n) {
    std::vector<Tree> l;
    for (; n; n >>= 1)
      l.push_back(of_bool(n % 2 == 1));
    return of_list(l);
  }

  std::string to_string(Tree x) {
    std::string result;
    for (Tree b : to_list(x))
      result += static_cast<char>(to_nat(b));
    return result;
  }

  Tree of_string(std::string s) {
    std::vector<Tree> l;
    for (char c : s)
      l.push_back(of_nat(c));
    return of_list(l);
  }

  Tree of_ternary(std::string s) {
    std::vector<Tree> stack;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
      char c = *it;
      switch (c) {
        case '0': stack.push_back(this->leaf()); break;
        case '1': {
          if (stack.empty()) throw std::runtime_error("of_ternary: stack underflow on '1'");
          Tree u = stack.back(); stack.pop_back();
          stack.push_back(this->stem(u));
          break;
        }
        case '2': {
          if (stack.size() < 2) throw std::runtime_error("of_ternary: stack underflow on '2'");
          Tree u = stack.back(); stack.pop_back();
          Tree v = stack.back(); stack.pop_back();
          stack.push_back(this->fork(u, v));
          break;
        }
        default:
          throw std::runtime_error("unexpected character in ternary encoding: " + std::string(1, c));
      }
    }
    if (stack.size() != 1) throw std::runtime_error("of_ternary: stack size is not 1 after decoding");
    return stack.back();
  }

  std::string to_ternary(Tree x) {
    std::string res;
    std::function<void(Tree)> walk = [&](Tree x) {
      this->template triage<int>(
        [&]() { res.push_back('0'); return 0; },
        [&](Tree u) { res.push_back('1'); walk(u); return 0; },
        [&](Tree u, Tree v) { res.push_back('2'); walk(u); walk(v); return 0; },
        x);
    };
    walk(x);
    return res;
  }
};
