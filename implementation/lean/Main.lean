import TreeCalculus.Tree

/-!
# Benchmark executable

Reads ternary-encoded values from stdin, one per line, applies them as a left
fold with the verified evaluator `Tree.applyOrStuck`, and prints the result in
ternary. Parsing and printing use explicit stacks, so input depth never
becomes C stack depth; `applyOrStuck` itself recurses once per nested application.
-/

open TreeCalculus Tree

/-- Fuel bounds `applyOrStuck`'s recursion depth, which the C stack bounds anyway. -/
def fuel : Nat := 2 ^ 62

/-- Ternary is prefix notation (`0` leaf, `1x` stem, `2xy` fork), so reading it
right to left leaves each node's children on top of the stack. -/
def parse (s : String) : Option Tree :=
  match s.foldr step (some []) with
  | some [t] => some t
  | _ => none
where
  step (c : Char) : Option (List Tree) → Option (List Tree)
    | some st => match c, st with
      | '0', st => some (leaf :: st)
      | '1', x :: st => some (stem x :: st)
      | '2', x :: y :: st => some (fork x y :: st)
      | _, _ => none
    | none => none

/-- Print a tree in ternary; `none` if it ran out of fuel. -/
partial def format (t : Tree) : Option String :=
  go [t] ""
where
  go : List Tree → String → Option String
    | [], acc => some acc
    | leaf :: st, acc => go st (acc.push '0')
    | stem x :: st, acc => go (x :: st) (acc.push '1')
    | fork x y :: st, acc => go (x :: y :: st) (acc.push '2')
    | stuck :: _, _ => none

def main : IO UInt32 := do
  let input ← (← IO.getStdin).readToEnd
  let lines := (input.splitOn "\n").map String.trim |>.filter (!·.isEmpty)
  let some (t :: ts) := lines.mapM parse
    | IO.eprintln "expected ternary-encoded trees, one per line"; return 1
  let some out := format (ts.foldl (applyOrStuck fuel) t)
    | IO.eprintln "out of fuel"; return 1
  IO.println out
  return 0
