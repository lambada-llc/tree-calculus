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

let%expect_test "omega" =
  let omega = "x" ^ (Ref "x" * Ref "x") |> to_tree in
  let open Tree in
  let open Sexp_of in
  print_s [%sexp (apply omega Leaf : t)];
  [%expect {| (() ()) |}];
  print_s [%sexp (apply omega (Stem Leaf) : t)];
  [%expect {| (() () (() ())) |}]

let%expect_test "if" =
  let if_ =
    "condition" ^ "then" ^ "else"
    ^ (triage (Ref "else") (Ref "then" |> k) Node * Ref "condition")
    |> to_tree
  in
  let open Tree in
  let open Sexp_of in
  let tree_of_bool = function false -> Leaf | true -> Stem Leaf in
  let if_ cond then_ else_ =
    apply (apply (apply if_ (tree_of_bool cond)) then_) else_
  in
  print_s [%sexp (if_ true Leaf (Stem Leaf) : t)];
  [%expect {| () |}];
  print_s [%sexp (if_ false Leaf (Stem Leaf) : t)];
  [%expect {| (() ()) |}]
