(* TC       |  OCaml
 * ---------+-------------
 * △        |  Leaf
 * △ a      |  Stem a
 * △ a b    |  Fork (a, b)
 *)
type t = Leaf | Stem of t | Fork of t * t
