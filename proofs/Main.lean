import TreeCalculus.Machine
import Std.Data.HashMap

/-!
# `lake exe machine <fuel>`: `step`, compiled, as a runner

`step` (`TreeCalculus/Machine.lean`) is what every theorem here is about; this runs that very
definition on DAG input, speaking the part of `runner -s`'s protocol
(`implementation/cpp/dag-machine/runner.md`) a differential test needs — so the eager runner
and the machine can be handed the same requests and their answers compared byte for byte
(`difftest.mjs`):

    reduce dag <byte-len>\n<bytes>   →   data <len>\n<bytes>, or err <message>\n
    quit\n                           →   ok\n

The payload is read as `parse_dag_into` reads it: each line `id f x` is one `apply(f, x)` —
here `step` iterated from `reduce f x []` until it returns `none` — and the line of one word
names the answer, printed as `to_dag` prints it. Every step of a request is paid from `<fuel>`;
a request that runs out answers `err out of fuel`, which is not an answer to compare.

Nothing here is imported by the proofs, so what it uses (`ptrAddrUnsafe`, below) is no
assumption of theirs.
-/

open TreeCalculus

/-- Iterate `step` from `s`, at most `fuel` times: the value it stops on, and the fuel left. -/
def exec : Nat → State Tree → Option (Tree × Nat)
  | 0, _ => none
  | n + 1, s => match step s with
    | some s' => exec n s'
    | none => match s with
      | .dispatch r _ => some (r, n)
      | .reduce .. => none                               -- `step` stops at `dispatch` only

/-- `parse_dag_into`: bind each line's value in turn; the value of the line of one word. Every
step is paid from the fuel the state holds. -/
def evalDag (text : String) : ExceptT String (StateM Nat) Tree := do
  let mut env : Std.HashMap String Tree := {}
  env := env.insert "△" .leaf
  for line in text.splitOn "\n" do
    let look (w : String) := match env[w]? with
      | some t => pure t
      | none => throw s!"unbound variable: {w}"
    match ((line.dropEndWhile (· == '\r')).toString.splitOn " ").filter (· ≠ "") with
    | [] => pure ()
    | [w] => return ← look w
    | [w, v] => env := env.insert w (← look v)
    | w :: f :: x :: _ =>
      let some (r, left) := exec (← get) (.reduce (← look f) (← look x) [])
        | set 0; throw "out of fuel"
      set left
      env := env.insert w r
  throw "not terminated by a value"

/-- `runner.cpp`'s `to_dag`, line for line, so equal trees print as equal bytes. It keys a node
by its index, which hash-consing makes a tree; this keys it by its address, which is finer — two
copies of a subtree are two keys — and prints the same: a copy is walked only once the first is
done, so every line it asks for is in `appKeys` already. Keyed by tree, a walk would take time
in the tree's expansion rather than its DAG. -/
unsafe def toDagImpl (root : Tree) : String := Id.run do
  let mut stack : Array (Tree × Bool) := #[(root, false)]   -- (node, exit phase)
  let mut keys : Std.HashMap USize String := {}
  let mut appKeys : Std.HashMap String String := {}
  let mut lines := ""
  let mut counter := 0
  while h : stack.size > 0 do
    let (node, exit) := stack[stack.size - 1]
    stack := stack.pop
    if keys.contains (ptrAddrUnsafe node) then continue
    let kids := match node with
      | .leaf => []
      | .stem u => [u]
      | .fork u v => [u, v]
    if !exit then
      -- Right child on top, so it is walked first — as `to_dag` does.
      stack := kids.foldl (fun s kid => s.push (kid, false)) (stack.push (node, true))
    else
      let mut current := "△"
      for kid in kids do
        let appKey := s!"{current} {keys.getD (ptrAddrUnsafe kid) ""}"
        current ← match appKeys[appKey]? with
          | some id => pure id
          | none => do
            let id := toString counter
            counter := counter + 1
            appKeys := appKeys.insert appKey id
            lines := lines ++ s!"{id} {appKey}\n"
            pure id
      keys := keys.insert (ptrAddrUnsafe node) current
  return lines ++ keys.getD (ptrAddrUnsafe root) ""

@[implemented_by toDagImpl] opaque toDag : Tree → String

/-- Exactly `n` bytes of `s`, or fewer at its end. -/
partial def readExact (s : IO.FS.Stream) (n : Nat) (acc : ByteArray := .empty) : IO ByteArray := do
  if acc.size ≥ n then return acc
  let chunk ← s.read (n - acc.size).toUSize
  if chunk.isEmpty then return acc
  readExact s n (acc ++ chunk)

def main : List String → IO UInt32
  | [fuel] => do
    let some fuel := fuel.toNat? | IO.eprintln "machine: fuel is a number of steps"; return 2
    let (stdin, stdout) := (← IO.getStdin, ← IO.getStdout)
    let reply (s : String) : IO Unit := do stdout.putStr s; stdout.flush
    repeat
      let line ← stdin.getLine
      if line.isEmpty then return 0
      match (line.dropEndWhile (· == '\n')).toString.splitOn " " with
      | ["reduce", "dag", n] =>
        let some n := n.toNat? | reply "err bad length\n"; return 1
        let some text := String.fromUTF8? (← readExact stdin n) | reply "err not UTF-8\n"
        let (answer, left) := (evalDag text).run.run fuel
        IO.eprintln s!"machine-stats: steps={fuel - left}"
        match answer with
        | .ok t => let out := toDag t; reply s!"data {out.utf8ByteSize}\n{out}"
        | .error e => reply s!"err {e}\n"
      | ["quit"] => reply "ok\n"; return 0
      | _ => reply s!"err unrecognized command\n"
    return 0
  | _ => do IO.eprintln "usage: machine <fuel>   (then runner -s requests on stdin)"; return 2
