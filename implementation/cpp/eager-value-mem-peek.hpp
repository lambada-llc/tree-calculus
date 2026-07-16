#pragma once

#include "eager-value-mem.hpp"
#include "peek.hpp"

// EagerValueMemPeek is the -peek reduction (peek.hpp) over EagerValueMem's
// three-vector storage.
using EagerValueMemPeek = Peek<EagerValueMem>;
