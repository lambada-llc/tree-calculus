#pragma once

#include <vector>
#include <functional>
#include <string>
#include <cstring>
#include <stdexcept>

// A tree is a boolean vector (bitstream) per "minimalist binary encoding":
// leaf = {1}, apply(A,B) = {0}+A+B.  So stem(x) = {0,1}+x, fork(x,y) = {0,0,1}+x+y.
//
// Reduction streams through a flat pre-allocated buffer (64M bits, ~64 MB).
// reduce() copies the vector in, runs streaming passes, copies the result out.
// Zero heap allocations during the core reduction loop.

class LazyAppStream {
public:
  using Tree = std::vector<bool>;

private:
  static constexpr size_t BUF_SIZE = 64 * 1024 * 1024;
  static inline bool buf_[BUF_SIZE];
  size_t head_;

  void emit_bit(bool b) { buf_[head_++] = b; }

  // Copy one subtree from buf[pos..] to head_. Returns position after it.
  size_t copy_tree(size_t pos) {
    for (size_t depth = 1; depth; ) { bool b = buf_[pos++]; emit_bit(b); b ? depth-- : depth++; }
    return pos;
  }

  // Skip one subtree at pos. Returns position after it.
  size_t skip_tree(size_t pos) const {
    for (size_t depth = 1; depth; ) buf_[pos++] ? depth-- : depth++;
    return pos;
  }

  // Relocate buf[from..head_) down to buf[to..], updating head_. (to < from)
  void relocate(size_t to, size_t from) {
    size_t n = head_ - from;
    std::memmove(buf_ + to, buf_ + from, n);
    head_ = to + n;
  }

  // Reduce tree at [begin..head_) in place.
  void reduce_in_place(size_t begin) {
    for (bool changed = true; changed; ) {
      changed = false;
      size_t out = head_;
      stream_reduce(begin, changed);
      relocate(begin, out);
    }
  }

  // Streaming pass: read subtree at buf[pos..], emit reduced form at head_.
  size_t stream_reduce(size_t pos, bool &changed) {
    if (buf_[pos]) { emit_bit(true); return pos + 1; }

    // Redex: apply(fork(Y, G), W) encoded as 0,0,0,1,Y..,G..,W..
    if (!buf_[pos+1] && !buf_[pos+2] && buf_[pos+3]) {
      changed = true;
      size_t y_pos = head_;
      size_t g_pos = copy_tree(pos + 4);     // copy Y to buffer; g_pos = source pos of G
      size_t w_pos = skip_tree(g_pos);       // w_pos = source pos of W
      reduce_in_place(y_pos);                // reduce Y in place at [y_pos, head_)
      size_t after_w = reduce_redex(g_pos, w_pos, y_pos, changed);
      return after_w;
    }

    // Not a redex: emit apply(A_reduced, B_reduced)
    emit_bit(false);
    pos = stream_reduce(pos + 1, changed);
    return stream_reduce(pos, changed);
  }

  // Emit redex result, overwriting Y at [y_pos..head_). Returns source pos after W.
  size_t reduce_redex(size_t g_pos, size_t w_pos, size_t y_pos, bool &changed) {

    // Y=leaf: → G.  Overwrite Y directly.
    if (buf_[y_pos]) {
      head_ = y_pos;
      copy_tree(g_pos);
      return skip_tree(w_pos);
    }

    // Y=stem(Y'): → @(@(Y',W), @(G,W)).  Overwrite Y directly.
    // Stream-reduce W once, then duplicate the reduced form.
    if (buf_[y_pos + 1]) {
      head_ = y_pos;
      emit_bit(false); emit_bit(false); copy_tree(y_pos + 2);
      size_t w_reduced = head_;
      size_t after_w = stream_reduce(w_pos, changed);  // stream-reduce W
      emit_bit(false);              copy_tree(g_pos);  copy_tree(w_reduced);
      return after_w;
    }

    // Y=fork(P,Q): must reduce W to inspect its shape.
    // Cannot overwrite Y yet — need P, Q positions while building result.
    size_t q_pos = skip_tree(y_pos + 3);
    size_t w_buf = head_;
    size_t after_w = copy_tree(w_pos);       // copy W from source
    reduce_in_place(w_buf);                  // reduce W in place

    size_t result = head_;
    if      (buf_[w_buf])     { copy_tree(y_pos + 3); }                             // W=leaf  → P
    else if (buf_[w_buf + 1]) { emit_bit(false); copy_tree(q_pos); copy_tree(w_buf + 2); }  // W=stem  → @(Q,D)
    else { size_t e_pos = skip_tree(w_buf + 3);                                     // W=fork  → @(@(G,D),E)
           emit_bit(false); emit_bit(false); copy_tree(g_pos); copy_tree(w_buf + 3); copy_tree(e_pos); }
    relocate(y_pos, result);                 // move result over Y + temp W
    return after_w;
  }

  void reduce(Tree &t) {
    head_ = 0;
    for (bool b : t) emit_bit(b);
    reduce_in_place(0);
    t.assign(buf_, buf_ + head_);
  }

public:
  LazyAppStream() : head_(0) {}

  std::string stats() { return "buffer " + std::to_string(BUF_SIZE/(1024*1024)) + "M bits"; }

  Tree leaf() { return {true}; }
  Tree stem(Tree u) { Tree r={false,true}; r.append_range(u); return r; }
  Tree fork(Tree u, Tree v) { Tree r={false,false,true}; r.append_range(u); r.append_range(v); return r; }

  template <typename T>
  T triage(std::function<T()> leaf_case, std::function<T(Tree)> stem_case, std::function<T(Tree, Tree)> fork_case, Tree x) {
    reduce(x);
    if (x[0]) return leaf_case();
    if (x[1]) return stem_case(Tree(x.begin()+2, x.end()));
    size_t pos = 3, depth = 1;
    while (depth) { if (x[pos++]) depth--; else depth++; }
    return fork_case(Tree(x.begin()+3, x.begin()+pos), Tree(x.begin()+pos, x.end()));
  }

  Tree apply(Tree a, Tree b) { Tree r={false}; r.append_range(a); r.append_range(b); return r; }
};
