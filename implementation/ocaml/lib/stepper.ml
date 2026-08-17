type t = Leaf | Stem of t | Fork of t * t | App of t * t

let rec of_tree = function
  | Tree.Leaf -> Leaf
  | Tree.Stem a -> Stem (of_tree a)
  | Tree.Fork (a, b) -> Fork (of_tree a, of_tree b)

let rec to_tree = function
  | Leaf -> Tree.Leaf
  | Stem a -> Tree.Stem (to_tree a)
  | Fork (a, b) -> Tree.Fork (to_tree a, to_tree b)
  | App _ ->
      failwith "Stepper.to_tree: term still has an outstanding application"

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

(* Rule 2 with the three peeks that avoid duplicating z into (x z) (y z):
 * x = K yields z outright (y z is never built), and an eliminator (K f) in
 * either position collapses its application to f. Each peek is a contraction
 * of the canonical normal-order sequence, so normal forms agree with [step]. *)
let peek_rule_2 x y z =
  let app f a = match f with Fork (Leaf, f) -> f | _ -> App (f, a) in
  match x with Stem Leaf -> z | _ -> App (app x z, app y z)

let step_peek a b =
  match a with Fork (Stem x, y) -> Some (peek_rule_2 x y b) | _ -> step a b

(* Leftmost-outermost: try the application here first, then recurse into the
 * function, then the argument. This is normal order, so it reaches a normal
 * form whenever one exists. Values (Stem/Fork) may still hold residual
 * applications, so we descend into them too. *)
let step_anywhere_with step =
  let rec go = function
    | Leaf -> None
    | Stem a -> Option.map (fun a -> Stem a) (go a)
    | Fork (a, b) -> (
        match go a with
        | Some a -> Some (Fork (a, b))
        | None -> Option.map (fun b -> Fork (a, b)) (go b))
    | App (a, b) -> (
        match step a b with
        | Some _ as reduced -> reduced
        | None -> (
            match go a with
            | Some a -> Some (App (a, b))
            | None -> Option.map (fun b -> App (a, b)) (go b)))
  in
  go

let step_anywhere = step_anywhere_with step
let step_anywhere_peek = step_anywhere_with step_peek

let reduce_with step_anywhere =
  let rec go e = match step_anywhere e with Some e -> go e | None -> e in
  go

let reduce = reduce_with step_anywhere
let reduce_peek = reduce_with step_anywhere_peek

let rec nodes = function
  | Leaf -> 1
  | Stem a -> 1 + nodes a
  | Fork (a, b) | App (a, b) -> 1 + nodes a + nodes b

(* Shrink-eager: fire the active application that shrinks the term most. Only
 * rule 2 grows a term (by the size of the argument it duplicates); every other
 * rule shrinks without duplicating, and contracting such a redex early can
 * only make every later term smaller (a residual argument). When nothing
 * shrinks, fall back to the leftmost-outermost active application -- not
 * proven normalizing, so [step_anywhere] stays the safe default. *)
let step_shrink_eager t =
  let candidates = ref [] in
  let rec go ctx t =
    match t with
    | Leaf -> ()
    | Stem a -> go (fun a -> ctx (Stem a)) a
    | Fork (a, b) ->
        go (fun a' -> ctx (Fork (a', b))) a;
        go (fun b' -> ctx (Fork (a, b'))) b
    | App (a, b) ->
        (match step a b with
        | Some r -> candidates := (ctx r, nodes r - nodes t) :: !candidates
        | None -> ());
        go (fun a' -> ctx (App (a', b))) a;
        go (fun b' -> ctx (App (a, b'))) b
  in
  go (fun x -> x) t;
  match List.filter (fun (_, d) -> d < 0) (List.rev !candidates) with
  | [] -> step_anywhere t (* nothing shrinks: normal order's pick *)
  | c :: shrinks ->
      Some
        (fst
           (List.fold_left
              (fun best c -> if snd c < snd best then c else best)
              c shrinks))

(* inline tests *)

open Core

(* Render a stepper term in the published △ notation: [△] is the only atom,
   application is implicit (a blank space), and it is left-associative. Stem and
   Fork are just [△] applied to one or two arguments, and an outstanding [App]
   prints exactly like any other application — so the notation is stable across
   the structural reductions that merely re-associate it. We flatten the
   left spine to a head (always [△]) plus an argument list, parenthesising any
   argument that is not itself a bare leaf. *)
let to_notation t =
  let rec spine t acc =
    match t with
    | Leaf -> acc
    | Stem a -> a :: acc
    | Fork (a, b) -> a :: b :: acc
    | App (a, b) -> spine a (b :: acc)
  in
  let rec render t =
    match spine t [] with
    | [] -> "\xe2\x96\xb3"
    | args -> String.concat ~sep:" " ("\xe2\x96\xb3" :: List.map args ~f:atom)
  and atom t =
    match t with Leaf -> "\xe2\x96\xb3" | _ -> "(" ^ render t ^ ")"
  in
  render t

(* Like [reduce] but prints each term in the sequence on its own line: the
   starting term, then the term after every step (so each step's "before" is the
   previous line and its "after" is the next). *)
let trace e =
  let rec go e =
    print_endline (to_notation e);
    match step_anywhere e with Some e -> go e | None -> e
  in
  go e

(* Number of [step_anywhere] steps to a normal form, for demonstrating the
   stepwise behaviour with values we can verify by hand. *)
let count_steps ?(step_anywhere = step_anywhere) e =
  let rec go n e =
    match step_anywhere e with Some e -> go (n + 1) e | None -> n
  in
  go 0 e

(* Like [trace] but for long reductions: print only every [k]-th state, with [k]
   chosen so at most [max_states] states are shown (the intermediate terms get
   enormous, so we sample rather than dump them all). Each printed state is
   truncated to [width] characters with its full length annotated, so the shape
   and scale are visible without committing megabytes. The final normal form is
   always printed (and is usually small). *)
let trace_sampled ?(max_states = 10) ?(width = 200) e =
  let total = count_steps e + 1 in
  let k = Int.max 1 ((total + max_states - 1) / max_states) in
  let show t =
    let s = to_notation t in
    if String.length s <= width then s
    else sprintf "%s… (%d chars)" (String.prefix s width) (String.length s)
  in
  let rec go i e =
    match step_anywhere e with
    | None ->
        print_endline (show e);
        e
    | Some e' ->
        if i % k = 0 then print_endline (show e);
        go (i + 1) e'
  in
  go 0 e

(* The stepper, run to termination, must agree with the eager reducer. *)
let agrees_with_reducer ?(reduce = reduce) a b =
  let via_stepper = to_tree (reduce (App (of_tree a, of_tree b))) in
  let reference = Tree.apply a b in
  Sexp.equal (Sexp_of.sexp_of_t via_stepper) (Sexp_of.sexp_of_t reference)

(* A tiny parser for the published △-and-parens notation: [△] is a leaf,
   juxtaposition is left-associative application, parens group. Lets us embed a
   tree program verbatim as a string instead of hand-building the constructors. *)
let parse s =
  let toks =
    s
    |> String.substr_replace_all ~pattern:"(" ~with_:" ( "
    |> String.substr_replace_all ~pattern:")" ~with_:" ) "
    |> String.substr_replace_all ~pattern:"\xe2\x96\xb3" ~with_:" L "
    |> String.split ~on:' '
    |> List.filter ~f:(Fn.non String.is_empty)
  in
  (* [atom] parses a leaf or a parenthesised expression; [app] folds a run of
     atoms left-associatively. *)
  let rec atom = function
    | "L" :: rest -> (Leaf, rest)
    | "(" :: rest -> (
        let e, rest = app rest in
        match rest with
        | ")" :: rest -> (e, rest)
        | _ -> failwith "parse: expected )")
    | _ -> failwith "parse: expected atom"
  and app toks =
    let first, rest = atom toks in
    let rec loop acc = function
      | (")" :: _ | []) as rest -> (acc, rest)
      | toks ->
          let a, rest = atom toks in
          loop (App (acc, a)) rest
    in
    loop first rest
  in
  match app toks with e, [] -> e | _ -> failwith "parse: trailing tokens"

let%expect_test "trace prints each term in the reduction sequence" =
  (* △ △ y z → y (rule 1, the K combinator): here y = △ △ and z = △, so the
     argument z is discarded and we are left with △ △. *)
  let _ = trace (App (Fork (Leaf, Stem Leaf), Leaf)) in
  [%expect {|
    △ △ (△ △) △
    △ △
    |}]

let%expect_test "trace of not true" =
  (* not = △ (△ (△△) (△△△)) △, applied to true = △△. Rule 3b applies first,
     then rule 1, reaching false = △. *)
  let not_tree = Fork (Fork (Stem Leaf, Fork (Leaf, Leaf)), Leaf) in
  let true_ = Stem Leaf in
  let _ = trace (App (not_tree, true_)) in
  [%expect {|
    △ (△ (△ △) (△ △ △)) △ (△ △)
    △ △ △ △
    △
    |}]

(* "size" program: applied to a tree it returns the node count as a chain. Embedded verbatim via [parse]. *)
let size =
  parse
    "△ (△ (△ (△ (△ (△ (△ △ (△ (△ (△ (△ (△ (△ (△ △)) △)) (△ △)))))) (△ △))) (△ \
     △ (△ (△ (△ △ (△ (△ (△ △ (△ (△ (△ △ (△ (△ (△ △ △)))))))) (△ (△ (△ (△ (△ △ \
     △)) (△ (△ (△ (△ △)) △)))) (△ (△ (△ (△ (△ △ (△ (△ (△ △ (△ (△ (△ △ △)) △))) \
     (△ △)))) (△ (△ (△ △ (△ (△ (△ △ △)) △))) (△ (△ (△ △ (△ (△ (△ △ (△ (△ (△ △ \
     △)) △))) (△ △)))))))) (△ (△ (△ △ (△ △))))))))) (△ (△ (△ (△ (△ △ (△ (△ (△ \
     (△ (△ (△ (△ △)) △)) (△ △)))))) (△ △)))))))) (△ △ △)"

let%expect_test "size program applied to a tree" =
  (* [size t] reduces to the node count of [t] as a unary stem-chain: each [△ _]
     wrapper is one unit, so n is [△ (△ (… △ △))] with n stems. So △ △ △ (a
     single fork, 3 nodes) gives 3 = [△ (△ (△ △))]. The two 3-node trees agree,
     as they must. *)
  List.iter [ "△"; "△ △"; "△ △ △"; "△ (△ △)"; "△ (△ △) △"; "△ (△ △) (△ △)" ]
    ~f:(fun s ->
      print_endline (s ^ "  =>  " ^ to_notation (reduce (App (size, parse s)))));
  [%expect
    {|
    △  =>  △ △
    △ △  =>  △ (△ △)
    △ △ △  =>  △ (△ (△ △))
    △ (△ △)  =>  △ (△ (△ △))
    △ (△ △) △  =>  △ (△ (△ (△ △)))
    △ (△ △) (△ △)  =>  △ (△ (△ (△ (△ △))))
    |}]

let%expect_test "trace of size applied to △ △ △" =
  (* Sample the reduction of [size (△ △ △)] down to the chain for 3. The
     intermediate terms are large, so only every k-th state is shown. *)
  let _ = trace_sampled (App (size, parse "△ △ △")) in
  [%expect
    {|
    △ (△ (△ (△ (△ (△ (△ △ (△ (△ (△ (△ (△ (△ (△ △)) △)) (△ △)))))) (△ △))) (△ △ (△ (△ (△ △ (△ (△ (△ △ (△ (△ (△ △ (△ (△ (△ △ △… (681 chars)
    △ (△ (△ (△ △ △)) (△ (△ (△ (△ △)) △)) (△ (△ (△ (△ (△ △ (△ (△ (△ (△ (△ (△ (△ △)) △)) (△ △)))))) (△ △))) (△ △ (△ △ (△ (△ (△ △… (2267 chars)
    △ (△ (△ (△ (△ (△ △)) △) (△ (△ (△ (△ (△ △ (△ (△ (△ (△ (△ (△ (△ △)) △)) (△ △)))))) (△ △))) (△ △ (△ (△ (△ △ (△ (△ (△ △ (△ (△ … (2223 chars)
    △ (△ (△ (△ (△ (△ △)) △) (△ (△ (△ (△ (△ △ (△ (△ (△ (△ (△ (△ (△ △)) △)) (△ △)))))) (△ △))) (△ △ (△ (△ (△ △ (△ (△ (△ △ (△ (△ … (2223 chars)
    △ (△ (△ (△ (△ (△ △)) △) (△ (△ (△ (△ (△ △ (△ (△ (△ (△ (△ (△ (△ △)) △)) (△ △)))))) (△ △))) (△ △ (△ (△ (△ △ (△ (△ (△ △ (△ (△ … (2777 chars)
    △ (△ (△ (△ (△ (△ △)) △) (△ (△ (△ (△ (△ △ (△ (△ (△ (△ (△ (△ (△ △)) △)) (△ △)))))) (△ △))) (△ △ (△ (△ (△ △ (△ (△ (△ △ (△ (△ … (2753 chars)
    △ (△ (△ (△ (△ (△ △)) △) (△ (△ (△ (△ (△ △ (△ (△ (△ (△ (△ (△ (△ △)) △)) (△ △)))))) (△ △))) (△ △ (△ (△ (△ △ (△ (△ (△ △ (△ (△ … (2753 chars)
    △ (△ (△ (△ (△ (△ △)) △) (△ (△ (△ (△ (△ △ (△ (△ (△ (△ (△ (△ (△ △)) △)) (△ △)))))) (△ △))) (△ △ (△ (△ (△ △ (△ (△ (△ △ (△ (△ … (2729 chars)
    △ (△ △ (△ (△ (△ (△ (△ △ (△ (△ (△ (△ (△ (△ (△ △)) △)) (△ △)))))) (△ △))) (△ △ (△ (△ (△ △ (△ (△ (△ △ (△ (△ (△ △ (△ (△ (△ △… (1401 chars)
    △ (△ (△ (△ (△ △ (△ (△ (△ (△ (△ (△ (△ △)) △)) (△ △)))))) (△ △) △ (△ △ (△ (△ (△ △ (△ (△ (△ △ (△ (△ (△ △ (△ (△ (△ △ △))))))))… (677 chars)
    △ (△ (△ △))
    |}]

let%expect_test "trace of size size" =
  (* [size size] computes the size of the size program itself. A long reduction
     over very large terms, sampled to at most ten states. *)
  let _ = trace_sampled (App (size, size)) in
  [%expect
    {|
    △ (△ (△ (△ (△ (△ (△ △ (△ (△ (△ (△ (△ (△ (△ △)) △)) (△ △)))))) (△ △))) (△ △ (△ (△ (△ △ (△ (△ (△ △ (△ (△ (△ △ (△ (△ (△ △ △… (1337 chars)
    △ (△ (△ (△ (△ (△ (△ (△ (△ △ (△ (△ (△ (△ (△ △ (△ (△ (△ (△ (△ (△ (△ △)) △)) (△ △)))))) (△ △))) (△ △ (△ (△ (△ △ (△ (△ (△ △ (�… (3237 chars)
    △ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ △ (△ (△ (△ △ (△ (△ (△ △ △)))))))) (△ (△ (△ (△ (△ △ △)) (�… (7597 chars)
    △ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ △ (△ (△ (△ △ (�… (9219 chars)
    △ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (… (11381 chars)
    △ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (… (10501 chars)
    △ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (… (11205 chars)
    △ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (… (9435 chars)
    △ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (… (7003 chars)
    △ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (… (3817 chars)
    △ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (△ (… (751 chars)
    |}]

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
  let reference = Tree.apply (to_tree f) (Tree.apply Tree.Leaf Tree.Leaf) in
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
  let not_tree =
    Tree.Fork
      ( Tree.Fork (Tree.Stem Tree.Leaf, Tree.Fork (Tree.Leaf, Tree.Leaf)),
        Tree.Leaf )
  in
  let id = "x" ^ Ref "x" |> to_tree in
  let k = "u" ^ "v" ^ Ref "u" |> to_tree in
  let funcs = [ Tree.Leaf; Tree.Stem Tree.Leaf; not_tree; id; k ] in
  let args =
    [
      Tree.Leaf; Tree.Stem Tree.Leaf; Tree.Fork (Tree.Leaf, Tree.Leaf); not_tree;
    ]
  in
  let all reduce =
    List.for_all funcs ~f:(fun f ->
        List.for_all args ~f:(fun a -> agrees_with_reducer ~reduce f a))
  in
  print_s [%sexp (all reduce : bool)];
  [%expect {| true |}];
  print_s [%sexp (all reduce_peek : bool)];
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

let%expect_test "peek variant: same normal forms, fewer steps" =
  (* The three peeks on hand-checkable values, with y = △ (△ △) and z = △ △.
     x = K: canonical takes 3 steps (rule 2, 0b, 1) and builds y z only to
     discard it; peek collapses to z in one, never building y z. *)
  let y = Stem (Stem Leaf) and z = Stem Leaf in
  print_s [%sexp (count_steps (App (Fork (Stem (Stem Leaf), y), z)) : int)];
  [%expect {| 3 |}];
  let show x = print_endline (to_notation (Option.value_exn (step_peek (Fork (Stem x, y)) z))) in
  show (Stem Leaf);
  [%expect {| △ △ |}];
  (* x an eliminator K f (f = △ △): f applied to y z, z not duplicated. *)
  show (Fork (Leaf, Stem Leaf));
  [%expect {| △ △ (△ (△ △) (△ △)) |}];
  (* y an eliminator K g: symmetric, x z applied to g. *)
  print_endline (to_notation (Option.value_exn (step_peek (Fork (Stem Leaf, Fork (Leaf, Stem Leaf))) z)));
  [%expect {| △ (△ △) (△ △) |}];
  (* Step counts on the size program: same results as the canonical stepper,
     via a compressed sequence. *)
  List.iter [ "△"; "△ △ △"; "△ (△ △) (△ △)" ] ~f:(fun s ->
      let t = App (size, parse s) in
      printf "size (%s): canonical %d, peek %d, both => %s\n" s (count_steps t)
        (count_steps ~step_anywhere:step_anywhere_peek t)
        (to_notation (reduce_peek t)));
  [%expect
    {|
    size (△): canonical 525, peek 523, both => △ △
    size (△ △ △): canonical 666, peek 623, both => △ (△ (△ △))
    size (△ (△ △) (△ △)): canonical 784, peek 707, both => △ (△ (△ (△ (△ △))))
    |}]

let%expect_test "shrink-eager: same normal form, no larger intermediates" =
  let peak stepper t =
    let rec go m t =
      match stepper t with None -> m | Some t -> go (Int.max m (nodes t)) t
    in
    go (nodes t) t
  in
  let t = App (size, parse "△ △ △") in
  let nf = reduce_with step_shrink_eager t in
  print_s
    [%sexp
      (Sexp.equal
         (Sexp_of.sexp_of_t (to_tree nf))
         (Sexp_of.sexp_of_t (to_tree (reduce t)))
        : bool)];
  [%expect {| true |}];
  printf "peak: root-first %d, shrink-eager %d\n" (peak step_anywhere t)
    (peak step_shrink_eager t);
  [%expect {| peak: root-first 1243, shrink-eager 561 |}]
