#pragma once

#include "eager-ternary-nil-mmap-32.hpp"
#include "peek.hpp"

// EagerTernaryNilMmap32Peek is the -peek reduction (peek.hpp) over the
// 32-bit-index mmap representation (8-byte nodes): the fastest representation
// combined with the peeking reduction, and the fastest evaluator in the suite.
using EagerTernaryNilMmap32Peek = Peek<EagerTernaryNilMmap32>;
