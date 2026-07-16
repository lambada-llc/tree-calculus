#pragma once

#include "eager-ternary-nil-mmap.hpp"
#include "peek.hpp"

// EagerTernaryNilMmapPeek is the -peek reduction (peek.hpp) over the tagless
// null-discriminated mmap representation with raw-pointer nodes.
using EagerTernaryNilMmapPeek = Peek<EagerTernaryNilMmap>;
