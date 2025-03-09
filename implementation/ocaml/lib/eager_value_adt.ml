open Tree

let rec apply a b =
  match a with
  | Leaf -> Stem b
  | Stem a -> Fork (a, b)
  | Fork (Leaf, a) -> a
  | Fork (Stem a1, a2) -> apply (apply a1 b) (apply a2 b)
  | Fork (Fork (a1, a2), a3) -> (
      match b with
      | Leaf -> a1
      | Stem u -> apply a2 u
      | Fork (u, v) -> apply (apply a3 u) v)
