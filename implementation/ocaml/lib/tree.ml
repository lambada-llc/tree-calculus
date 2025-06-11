(* TC       |  OCaml
 * ---------+-------------
 * △        |  Leaf
 * △ a      |  Stem a
 * △ a b    |  Fork (a, b)
 *)
type t = Leaf | Stem of t | Fork of t * t

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

(* inline tests *)

open Core

let%expect_test "not" =
  let not_tree = Fork (Fork (Stem Leaf, Fork (Leaf, Leaf)), Leaf) in
  let bool_of_tree = function Leaf -> false | _ -> true in
  let tree_of_bool = function false -> Leaf | true -> Stem Leaf in
  let not_ b = tree_of_bool b |> apply not_tree |> bool_of_tree in
  print_s [%sexp (not_ false : bool)];
  [%expect {| true |}];
  print_s [%sexp (not_ true : bool)];
  [%expect {| false |}]
