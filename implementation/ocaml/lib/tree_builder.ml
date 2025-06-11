open Core

type t =
  | Ref of string (* variables are indexed by strings *)
  | Node
  | App of (t * t)
[@@deriving equal]

let rec occurs x m =
  match m with
  | Ref y -> [%equal: string] x y
  | App (m1, m2) -> occurs x m1 || occurs x m2
  | _ -> false

let ( * ) x y = App (x, y) (* for compact notation *)

(* common combinators *)
let k u = Node * Node * u
let s u v = Node * (Node * u) * v
let triage u v w = Node * (Node * u * v) * w
let i = s (Node * Node) Node

let rec star_abstraction x m =
  match occurs x m with
  | false -> k m
  | true -> (
      match m with
      | Ref _ -> i
      | App (m1, Ref _) when not (occurs x m1) ->
          (* η-reduction *)
          m1
      | App (m1, m2) -> s (star_abstraction x m1) (star_abstraction x m2)
      | _ -> failwith "unreachable")

let ( ^ ) = star_abstraction (* for compact notation *)

let rec to_tree = function
  | Ref x -> raise_s [%sexp "unbound variable", (x : string)]
  | Node -> Tree.Leaf
  | App (m1, m2) -> Tree.apply (to_tree m1) (to_tree m2)

let rec of_tree = function
  | Tree.Leaf -> Node
  | Stem t -> Node * of_tree t
  | Fork (t1, t2) -> Node * of_tree t1 * of_tree t2

(* inline tests *)

let%expect_test "id" =
  let id = "x" ^ Ref "x" |> to_tree in
  let open Tree in
  let open Sexp_of in
  print_s [%sexp (apply id Leaf : t)];
  [%expect {| () |}];
  print_s [%sexp (apply id (Stem Leaf) : t)];
  [%expect {| (() ()) |}];
  print_s [%sexp (apply id (Fork (Leaf, Leaf)) : t)];
  [%expect {| (() () ()) |}]

let%expect_test "not" =
  let not_ =
    "b"
    ^ triage
        (Marshal.tree_of_bool true |> of_tree)
        (Marshal.tree_of_bool false |> of_tree |> k)
        Node
      * Ref "b"
    |> to_tree
  in
  let open Tree in
  print_s
    [%sexp
      (apply not_ (Marshal.tree_of_bool true) |> Marshal.bool_of_tree : bool)];
  [%expect {| false |}];
  print_s
    [%sexp
      (apply not_ (Marshal.tree_of_bool false) |> Marshal.bool_of_tree : bool)];
  [%expect {| true |}]
