import TreeCalculus.Tree

/-!
# Benchmark executable

Reads ternary-encoded values from stdin, one per line, applies them as a left
fold with one of the formalization's verified evaluators, and prints the result
in ternary. `--list` names the evaluators, `--evaluator NAME` picks one (default:
the fastest, `Tree.applyOrStuck`). Parsing and printing use explicit stacks, so
input depth never becomes C stack depth; the evaluators themselves recurse once
per nested application.
-/

open TreeCalculus

/-- Fuel bounds the evaluators' recursion depth, which the C stack bounds anyway. -/
def fuel : Nat := 2 ^ 62

/-- What printing needs to see of a value. -/
inductive Shape (α : Type) | leaf | stem (x : α) | fork (x y : α)

/-- A representation of trees, and an evaluator over it. -/
structure Evaluator where
  {α : Type}
  leaf : α
  stem : α → α
  fork : α → α → α
  /-- `none` for anything that is not a value (`stuck`, say). -/
  view : α → Option (Shape α)
  /-- `none` when out of fuel. -/
  apply : α → α → Option α

/-- Ternary is prefix notation (`0` leaf, `1x` stem, `2xy` fork), so reading it
right to left leaves each node's children on top of the stack. -/
def Evaluator.parse (e : Evaluator) (s : String) : Option e.α :=
  match s.foldr step (some []) with
  | some [t] => some t
  | _ => none
where
  step (c : Char) : Option (List e.α) → Option (List e.α)
    | some st => match c, st with
      | '0', st => some (e.leaf :: st)
      | '1', x :: st => some (e.stem x :: st)
      | '2', x :: y :: st => some (e.fork x y :: st)
      | _, _ => none
    | none => none

/-- Print a value in ternary; `none` if it is not one. -/
partial def Evaluator.format (e : Evaluator) (t : e.α) : Option String :=
  go [t] ""
where
  go : List e.α → String → Option String
    | [], acc => some acc
    | t :: st, acc => match e.view t with
      | some .leaf => go st (acc.push '0')
      | some (.stem x) => go (x :: st) (acc.push '1')
      | some (.fork x y) => go (x :: y :: st) (acc.push '2')
      | none => none

namespace Term

def view : Term → Option (Shape Term)
  | .leaf => some .leaf
  | .app .leaf x => some (.stem x)
  | .app (.app .leaf x) y => some (.fork x y)
  | _ => none

def evaluator (apply : Term → Term → Option Term) : Evaluator :=
  { leaf := △, stem := (△ ⬝ ·), fork := (△ ⬝ · ⬝ ·), view, apply }

end Term

def Tree.view : Tree → Option (Shape Tree)
  | .leaf => some .leaf
  | .stem x => some (.stem x)
  | .fork x y => some (.fork x y)
  | .stuck => none

/-- Fastest first: the first is the default. `stuck` is never a value, so an
`OrStuck` evaluator that runs out of fuel fails to print, like `none` would. -/
def evaluators : List (String × Evaluator) :=
  [ ("Tree.applyOrStuck",
      { leaf := .leaf, stem := .stem, fork := .fork, view := Tree.view,
        apply := fun a b => some (Tree.applyOrStuck fuel a b) }),
    ("Term.applyOrStuck",
      Term.evaluator fun a b => some (Term.applyOrStuck fuel a b)),
    ("Term.applyWithFuel", Term.evaluator (Term.applyWithFuel fuel)) ]

def run (e : Evaluator) (input : String) : Except String String := do
  let lines := (input.splitOn "\n").map String.trim |>.filter (!·.isEmpty)
  let some (t :: ts) := lines.mapM e.parse
    | throw "expected ternary-encoded trees, one per line"
  let some out := ts.foldlM e.apply t >>= e.format
    | throw "out of fuel"
  return out

def main (args : List String) : IO UInt32 := do
  let name ← match args with
    | [] => pure "Tree.applyOrStuck"
    | ["--evaluator", name] => pure name
    | ["--list"] => do
      evaluators.forM (IO.println ·.1); return 0
    | _ => IO.eprintln "usage: tree-calculus [--list | --evaluator NAME]"; return 1
  let some e := evaluators.lookup name
    | IO.eprintln s!"unknown evaluator: {name}"; return 1
  match run e (← (← IO.getStdin).readToEnd) with
  | .ok out => IO.println out; return 0
  | .error msg => IO.eprintln msg; return 1
