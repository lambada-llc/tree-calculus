open Core
open Tree

(* Conventions for turning OCaml data types into trees or vice versa *)

let bool_of_tree = function
  | Leaf -> false
  | Stem _ -> true
  | Fork _ -> failwith "bool_of_tree: unexpected Fork"

let tree_of_bool = function false -> Leaf | true -> Stem Leaf

let rec list_of_tree elem_of_tree = function
  | Leaf -> []
  | Fork (t1, t2) -> elem_of_tree t1 :: list_of_tree elem_of_tree t2
  | Stem _ -> failwith "seq_of_tree: unexpected Stem"

let rec tree_of_list tree_of_elem seq =
  match seq with
  | [] -> Leaf
  | x :: xs -> Fork (tree_of_elem x, tree_of_list tree_of_elem xs)

let int_of_tree t =
  list_of_tree bool_of_tree t
  |> List.fold_right ~init:0 ~f:(fun bit acc ->
         (if bit then 1 else 0) + (2 * acc))

let tree_of_int n =
  let rec int_to_bool_list n =
    if n = 0 then [] else (n mod 2 = 1) :: int_to_bool_list (n / 2)
  in
  int_to_bool_list n |> tree_of_list tree_of_bool

let char_of_tree t = int_of_tree t |> Char.of_int_exn
let tree_of_char c = Char.to_int c |> tree_of_int
let string_of_tree t = list_of_tree char_of_tree t |> String.of_char_list
let tree_of_string s = String.to_list s |> tree_of_list tree_of_char

(* inline tests *)

open Sexp_of

let%expect_test "bool conv" =
  print_s [%sexp (tree_of_bool false : t)];
  [%expect {| () |}];
  print_s [%sexp (tree_of_bool true : t)];
  [%expect {| (() ()) |}];
  print_s [%sexp (tree_of_bool false |> bool_of_tree : bool)];
  [%expect {| false |}];
  print_s [%sexp (tree_of_bool true |> bool_of_tree : bool)];
  [%expect {| true |}]

let%expect_test "int conv" =
  let open Sexp_of in
  print_s [%sexp (tree_of_int 0 : t)];
  [%expect {| () |}];
  print_s [%sexp (tree_of_int 13 : t)];
  [%expect {| (() (() ()) (() () (() (() ()) (() (() ()) ())))) |}];
  print_s [%sexp (tree_of_int 0 |> int_of_tree : int)];
  [%expect {| 0 |}];
  print_s [%sexp (tree_of_int 13 |> int_of_tree : int)];
  [%expect {| 13 |}]

let%expect_test "string conv" =
  let open Sexp_of in
  print_s [%sexp (tree_of_string "" : t)];
  [%expect {| () |}];
  print_s [%sexp (tree_of_string "AB!" : t)];
  [%expect
    {|
    (() (() (() ()) (() () (() () (() () (() () (() () (() (() ()) ())))))))
     (() (() () (() (() ()) (() () (() () (() () (() () (() (() ()) ())))))))
      (() (() (() ()) (() () (() () (() () (() () (() (() ()) ())))))) ())))
    |}];
  print_s [%sexp (tree_of_string "" |> string_of_tree : string)];
  [%expect {| "" |}];
  print_s [%sexp (tree_of_string "AB!" |> string_of_tree : string)];
  [%expect {| AB! |}]

let%expect_test "ppx tree_of" =
  print_s ~mach:() [%sexp (true |> [%tree_of: bool] : t)];
  [%expect {| (()()) |}];
  print_s ~mach:() [%sexp (65 |> [%tree_of: int] : t)];
  [%expect {| (()(()())(()()(()()(()()(()()(()()(()(()())()))))))) |}];
  print_s ~mach:() [%sexp ('A' |> [%tree_of: char] : t)];
  [%expect {| (()(()())(()()(()()(()()(()()(()()(()(()())()))))))) |}];
  print_s ~mach:() [%sexp ("A" |> [%tree_of: string] : t)];
  [%expect {| (()(()(()())(()()(()()(()()(()()(()()(()(()())())))))))()) |}];
  print_s ~mach:() [%sexp ([ 'A' ] |> [%tree_of: char list] : t)];
  [%expect {| (()(()(()())(()()(()()(()()(()()(()()(()(()())())))))))()) |}]

let%expect_test "ppx of_tree" =
  let tree_true = [%tree_of: bool] true in
  print_s ~mach:() [%sexp (tree_true |> [%of_tree: bool] : bool)];
  [%expect {| true |}];
  let tree_65 = [%tree_of: int] 65 in
  print_s ~mach:() [%sexp (tree_65 |> [%of_tree: int] : int)];
  [%expect {| 65 |}];
  print_s ~mach:() [%sexp (tree_65 |> [%of_tree: char] : char)];
  [%expect {| A |}];
  print_s ~mach:() [%sexp (tree_65 |> [%of_tree: bool list] : bool list)];
  [%expect {| (true false false false false false true) |}];
  let tree_foo = [%tree_of: string] "foo" in
  print_s ~mach:() [%sexp (tree_foo |> [%of_tree: string] : string)];
  [%expect {| foo |}];
  print_s ~mach:() [%sexp (tree_foo |> [%of_tree: char list] : char list)];
  [%expect {| (f o o) |}];
  print_s ~mach:()
    [%sexp (tree_foo |> [%of_tree: bool list list] : bool list list)];
  [%expect
    {| ((false true true false false true true)(true true true true false true true)(true true true true false true true)) |}]
