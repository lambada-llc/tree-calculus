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

(* inline tests *)

open Core

let%expect_test "not" =
  let not_tree = Fork (Fork (Stem Leaf, Fork (Leaf, Leaf)), Leaf) in
  let not_ b =
    Marshal.tree_of_bool b |> apply not_tree |> Marshal.bool_of_tree
  in
  let open Core in
  print_s [%sexp (not_ false : bool)];
  [%expect {| true |}];
  print_s [%sexp (not_ true : bool)];
  [%expect {| false |}]
