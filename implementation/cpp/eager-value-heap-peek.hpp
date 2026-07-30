#pragma once

#include "eager-value-heap.hpp"
#include "peek.hpp"

// EagerValueHeapPeek is the -peek reduction (peek.hpp) over EagerValueHeap's
// refcounted heap nodes.
using EagerValueHeapPeek = Peek<EagerValueHeap>;
