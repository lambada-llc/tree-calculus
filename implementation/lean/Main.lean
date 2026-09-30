import TreeCalculus.Eval

/-!
# Benchmark executable

Reads ternary-encoded values from stdin, one per line, applies them as a left
fold with the verified evaluator `applyF`, and prints the result in ternary.
Parsing and printing use explicit stacks, so input depth never becomes C stack
depth; `applyF` itself recurses once per nested application.
-/

open TreeCalculus Term

/-- Fuel bounds `applyF`'s recursion depth, which the C stack bounds anyway. -/
def fuel : Nat := 2 ^ 62

/-- Ternary is prefix notation (`0` leaf, `1x` stem, `2xy` fork), so reading it
right to left leaves each node's children on top of the stack. -/
def parse (s : String) : Option Term :=
  match s.foldr step (some []) with
  | some [t] => some t
  | _ => none
where
  step (c : Char) : Option (List Term) → Option (List Term)
    | some st => match c, st with
      | '0', st => some (△ :: st)
      | '1', x :: st => some ((△ ⬝ x) :: st)
      | '2', x :: y :: st => some ((△ ⬝ x ⬝ y) :: st)
      | _, _ => none
    | none => none

/-- Print a value in ternary; `none` if it is not one. -/
partial def format (t : Term) : Option String :=
  go [t] ""
where
  go : List Term → String → Option String
    | [], acc => some acc
    | .leaf :: st, acc => go st (acc.push '0')
    | .app .leaf x :: st, acc => go (x :: st) (acc.push '1')
    | .app (.app .leaf x) y :: st, acc => go (x :: y :: st) (acc.push '2')
    | _, _ => none

def main : IO UInt32 := do
  let input ← (← IO.getStdin).readToEnd
  let lines := (input.splitOn "\n").map String.trim |>.filter (!·.isEmpty)
  let some (t :: ts) := lines.mapM parse
    | IO.eprintln "expected ternary-encoded trees, one per line"; return 1
  let some v := ts.foldlM (applyF fuel) t
    | IO.eprintln "out of fuel"; return 1
  let some out := format v
    | IO.eprintln "result is not a value"; return 1
  IO.println out
  return 0
