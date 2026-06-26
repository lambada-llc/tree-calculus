type t = Leaf | Stem of t | Fork of t * t | App of t * t

let rec of_tree = function
  | Tree.Leaf -> Leaf
  | Tree.Stem a -> Stem (of_tree a)
  | Tree.Fork (a, b) -> Fork (of_tree a, of_tree b)

let rec to_tree = function
  | Leaf -> Tree.Leaf
  | Stem a -> Tree.Stem (to_tree a)
  | Fork (a, b) -> Tree.Fork (to_tree a, to_tree b)
  | App _ -> failwith "Stepper.to_tree: term still has an outstanding application"

(* One step of the explicit-application rules. The shape we need to classify the
 * redex is: the function [a]'s top constructor (and its left child, to tell
 * rules 1/2/3 apart) and, for rule 3, the argument [b]'s top constructor. When
 * either is still an [App] we cannot pick a rule yet, so the application is
 * inactive and we return [None]. Note the rules select sub-terms as-is: the
 * selected pieces may themselves contain applications, which later steps reduce.
 *)
let step a b =
  match (a, b) with
  | App _, _ -> None (* function not a value yet *)
  | Fork (App _, _), _ -> None (* can't tell rule 1 vs 2 vs 3 yet *)
  | Leaf, z -> Some (Stem z) (* 0a *)
  | Stem y, z -> Some (Fork (y, z)) (* 0b *)
  | Fork (Leaf, y), _ -> Some y (* 1 *)
  | Fork (Stem x, y), z -> Some (App (App (x, z), App (y, z))) (* 2 *)
  | Fork (Fork (w, _), _), Leaf -> Some w (* 3a *)
  | Fork (Fork (_, x), _), Stem u -> Some (App (x, u)) (* 3b *)
  | Fork (Fork (_, _), y), Fork (u, v) -> Some (App (App (y, u), v)) (* 3c *)
  | Fork (Fork _, _), App _ -> None (* rule-3 argument not a value yet *)

(* Leftmost-outermost: try the application here first, then recurse into the
 * function, then the argument. This is normal order, so it reaches a normal
 * form whenever one exists. Values (Stem/Fork) may still hold residual
 * applications, so we descend into them too. *)
let rec step_anywhere = function
  | Leaf -> None
  | Stem a -> Option.map (fun a -> Stem a) (step_anywhere a)
  | Fork (a, b) -> (
      match step_anywhere a with
      | Some a -> Some (Fork (a, b))
      | None -> Option.map (fun b -> Fork (a, b)) (step_anywhere b))
  | App (a, b) -> (
      match step a b with
      | Some _ as reduced -> reduced
      | None -> (
          match step_anywhere a with
          | Some a -> Some (App (a, b))
          | None -> Option.map (fun b -> App (a, b)) (step_anywhere b)))

let rec reduce e =
  match step_anywhere e with Some e -> reduce e | None -> e

(* inline tests *)

open Core

(* Number of [step_anywhere] steps to a normal form, for demonstrating the
   stepwise behaviour with values we can verify by hand. *)
let count_steps e =
  let rec go n e =
    match step_anywhere e with Some e -> go (n + 1) e | None -> n
  in
  go 0 e

(* The stepper, run to termination, must agree with the eager reducer. *)
let agrees_with_reducer a b =
  let via_stepper = to_tree (reduce (App (of_tree a, of_tree b))) in
  let reference = Tree.apply a b in
  Sexp.equal (Sexp_of.sexp_of_t via_stepper) (Sexp_of.sexp_of_t reference)

let%expect_test "step counts (not)" =
  (* not = △ (△ (△△) (△△△)) △, the example used elsewhere in this repo. *)
  let not_tree = Fork (Fork (Stem Leaf, Fork (Leaf, Leaf)), Leaf) in
  let false_ = Leaf and true_ = Stem Leaf in
  (* not false → true via one step (rule 3a). *)
  print_s [%sexp (count_steps (App (not_tree, false_)) : int)];
  [%expect {| 1 |}];
  (* not true → false via two steps (rule 3b, then rule 1). *)
  print_s [%sexp (count_steps (App (not_tree, true_)) : int)];
  [%expect {| 2 |}]

let%expect_test "step distinguishes active from inactive applications" =
  (* A rule-3 shaped function whose argument is itself an unreduced
     application: the top application is NOT yet active. *)
  let f = Fork (Fork (Leaf, Leaf), Leaf) in
  let arg = App (Leaf, Leaf) in
  print_s [%sexp (Option.is_some (step f arg) : bool)];
  [%expect {| false |}];
  (* Once the argument is reduced to a value, the same application is active. *)
  print_s [%sexp (Option.is_some (step f (reduce arg)) : bool)];
  [%expect {| true |}];
  (* An application whose function is still an application is inactive too. *)
  print_s [%sexp (Option.is_some (step (App (Leaf, Leaf)) Leaf) : bool)];
  [%expect {| false |}];
  (* But step_anywhere still makes progress inside the inactive term, and
     reduce drives it to the same value as the eager reducer. *)
  let nested = App (f, arg) in
  let reference =
    Tree.apply (to_tree f) (Tree.apply Tree.Leaf Tree.Leaf)
  in
  print_s
    [%sexp
      (Sexp.equal
         (Sexp_of.sexp_of_t (to_tree (reduce nested)))
         (Sexp_of.sexp_of_t reference)
        : bool)];
  [%expect {| true |}]

let%expect_test "stepper agrees with Tree.apply" =
  let open Tree_builder in
  (* A spread of values: leaf, the booleans, combinators, and the not program,
     applied to a spread of arguments. *)
  let not_tree = Tree.Fork (Tree.Fork (Tree.Stem Tree.Leaf, Tree.Fork (Tree.Leaf, Tree.Leaf)), Tree.Leaf) in
  let id = "x" ^ Ref "x" |> to_tree in
  let k = k (Ref "u") |> star_abstraction "u" |> to_tree in
  let funcs = [ Tree.Leaf; Tree.Stem Tree.Leaf; not_tree; id; k ] in
  let args =
    [ Tree.Leaf; Tree.Stem Tree.Leaf; Tree.Fork (Tree.Leaf, Tree.Leaf); not_tree ]
  in
  let all =
    List.for_all funcs ~f:(fun f ->
        List.for_all args ~f:(fun a -> agrees_with_reducer f a))
  in
  print_s [%sexp (all : bool)];
  [%expect {| true |}]

let%expect_test "stepper agrees with reducer on a recursive program" =
  (* The exp program from the Memoize tests: exp n returns a tree with 2^n
     leaves. A good stress test that the stepper reaches the same value as the
     eager reducer on a non-trivial recursive computation. *)
  let exp_tree =
    Of_sexp.t_of_sexp
      (Sexp.of_string
         "(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(()))))(()(())(()(()(()(())(()(()(()(()(()(())(()(()(()(())(())))(()(())))))(()(()(()(()(()(())(()(()(()(())(())))(()))))(()(()(()(())(()))))))(()(()(()(())))(())))))(()(())(())))))(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(())))))))(()(()))))))")
  in
  let same n =
    let n_tree = Marshal.tree_of_small_int n in
    agrees_with_reducer exp_tree n_tree
  in
  print_s [%sexp (List.for_all [ 0; 1; 2; 3; 4 ] ~f:same : bool)];
  [%expect {| true |}]
