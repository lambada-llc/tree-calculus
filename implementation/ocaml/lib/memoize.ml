open Core
open Tree

module Id = struct
  include Int
end

module Cost = struct
  type t = {
    theoretical_num_apps : int;
    theoretical_num_allocs : int;
    num_apps : int;
    num_allocs : int;
    num_cache_hits : int;
  }
  [@@deriving sexp_of]

  let zero =
    {
      theoretical_num_apps = 0;
      theoretical_num_allocs = 0;
      num_apps = 0;
      num_allocs = 0;
      num_cache_hits = 0;
    }

  let alloc = { zero with theoretical_num_allocs = 1; num_allocs = 1 }

  let inc_app t =
    {
      t with
      theoretical_num_apps = t.theoretical_num_apps + 1;
      num_apps = t.num_apps + 1;
    }

  let as_cache_hit t =
    { t with num_cache_hits = 1; num_apps = 0; num_allocs = 0 }

  let ( + ) a b =
    {
      theoretical_num_apps = a.theoretical_num_apps + b.theoretical_num_apps;
      theoretical_num_allocs =
        a.theoretical_num_allocs + b.theoretical_num_allocs;
      num_apps = a.num_apps + b.num_apps;
      num_allocs = a.num_allocs + b.num_allocs;
      num_cache_hits = a.num_cache_hits + b.num_cache_hits;
    }
end

module App = struct
  module M = struct
    type t = Id.t * Id.t [@@deriving compare, sexp, hash]
  end

  include M
  include Comparable.Make (M)
end

module App_res = struct
  type t = { id : Id.t; cost : Cost.t } [@@deriving sexp_of]
end

module Shallow_node = struct
  type t = Leaf | Stem of Id.t | Fork of Id.t * Id.t
  [@@deriving compare, sexp, hash]
end

type t = { decode : Shallow_node.t Map.M(Id).t; cache : App_res.t Map.M(App).t }
[@@deriving sexp_of]

let alloc t x =
  let id = Map.length t.decode in
  let decode = Map.set t.decode ~key:id ~data:x in
  ({ t with decode }, { App_res.id; cost = Cost.alloc })

let empty =
  {
    decode = Map.singleton (module Id) 0 Shallow_node.Leaf;
    cache = Map.empty (module App);
  }

let rec apply t a b =
  let open Shallow_node in
  match Map.find t.cache (a, b) with
  | Some res -> (t, { res with cost = Cost.as_cache_hit res.cost })
  | None ->
      let t, res =
        match Map.find_exn t.decode a with
        | Leaf -> alloc t (Stem b)
        | Stem a -> alloc t (Fork (a, b))
        | Fork (x, y) -> (
            match Map.find_exn t.decode x with
            | Leaf -> (t, { App_res.id = y; cost = Cost.zero })
            | Stem a1 ->
                let t, { App_res.id = a1; cost = c1 } = apply t a1 b in
                let t, { App_res.id = y; cost = c2 } = apply t y b in
                let t, { App_res.id = res; cost = c3 } = apply t a1 y in
                (t, { App_res.id = res; cost = Cost.(c1 + c2 + c3) })
            | Fork (a1, a2) -> (
                match Map.find_exn t.decode b with
                | Leaf -> (t, { App_res.id = a1; cost = Cost.zero })
                | Stem u ->
                    let t, res = apply t a2 u in
                    (t, res)
                | Fork (u, v) ->
                    let t, { App_res.id = res; cost = c1 } = apply t y u in
                    let t, { App_res.id = res; cost = c2 } = apply t res v in
                    (t, { App_res.id = res; cost = Cost.(c1 + c2) })))
      in
      let res = { res with cost = Cost.inc_app res.cost } in
      ({ t with cache = Map.set t.cache ~key:(a, b) ~data:res }, res)

let rec encode t tree =
  match tree with
  | Leaf -> (t, 0)
  | Stem a ->
      let t, a = encode t a in
      let t, { App_res.id = res; cost = _ } = apply t 0 a in
      (t, res)
  | Fork (a, b) ->
      let t, a = encode t a in
      let t, b = encode t b in
      let t, { App_res.id = res; cost = _ } = apply t 0 a in
      let t, { App_res.id = res; cost = _ } = apply t res b in
      (t, res)

let rec decode t id =
  match Map.find_exn t.decode id with
  | Leaf -> Leaf
  | Stem a -> Stem (decode t a)
  | Fork (a, b) -> Fork (decode t a, decode t b)

let%expect_test "not" =
  let not_tree = Fork (Fork (Stem Leaf, Fork (Leaf, Leaf)), Leaf) in
  let t = empty in
  let t, not_id = encode t not_tree in
  let t, false_id = encode t Leaf in
  let t, true_id = encode t (Stem Leaf) in
  let t, { App_res.id = not_false_id; cost = _ } = apply t not_id false_id in
  let t, { App_res.id = not_true_id; cost = _ } = apply t not_id true_id in
  print_s [%sexp (false_id : Id.t)];
  print_s [%sexp (true_id : Id.t)];
  print_s [%sexp (not_id : Id.t)];
  [%expect {|
    0
    1
    6
    |}];
  print_s ~mach:() [%sexp (not_tree : Sexp_of.t)];
  print_s ~mach:() [%sexp (decode t not_id : Sexp_of.t)];
  [%expect {|
    (()(()(()())(()()()))())
    (()(()(()())(()()()))())
    |}];
  print_s [%sexp (not_false_id : Id.t)];
  print_s ~mach:() [%sexp (Tree.apply not_tree Leaf : Sexp_of.t)];
  print_s ~mach:() [%sexp (decode t not_false_id : Sexp_of.t)];
  [%expect {|
    1
    (()())
    (()())
    |}];
  print_s [%sexp (not_true_id : Id.t)];
  print_s ~mach:() [%sexp (Tree.apply not_tree (Stem Leaf) : Sexp_of.t)];
  print_s ~mach:() [%sexp (decode t not_true_id : Sexp_of.t)];
  [%expect {|
    0
    ()
    ()
    |}];
  print_s [%sexp (t.decode : Shallow_node.t Map.M(Id).t)];
  [%expect
    {|
    ((0 Leaf) (1 (Stem 0)) (2 (Fork 0 0)) (3 (Stem 1)) (4 (Fork 1 2))
     (5 (Stem 4)) (6 (Fork 4 0)))
    |}]

let%expect_test "exp" =
  let exp_tree =
    (* Program that takes a small nat n and returns a tree with 2^n leafs.
       Lambada syntax: exp = fix $ \self triage △ (\n △ (self n) (self n)) △ *)
    Of_sexp.t_of_sexp
      (Sexp.of_string
         "(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(()))))(()(())(()(()(()(())(()(()(()(()(()(())(()(()(()(())(())))(()(())))))(()(()(()(()(()(())(()(()(()(())(())))(()))))(()(()(()(())(()))))))(()(()(()(())))(())))))(()(())(())))))(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(())))))))(()(()))))))")
  in
  let rec num_leafs = function
    | Leaf -> 1
    | Stem a -> num_leafs a
    | Fork (a, b) -> num_leafs a + num_leafs b
  in
  let t = empty in
  let t, exp_id = encode t exp_tree in
  let test t n =
    let t, n_id = encode t (Marshal.tree_of_small_int n) in
    let t, { App_res.id = res_id; cost } = apply t exp_id n_id in
    print_s ~mach:()
      [%sexp
        "exp",
        (n : int),
        "=",
        (decode t res_id |> num_leafs : int),
        ",",
        "cost",
        (cost : Cost.t)];
    t
  in
  (* we expect exponential theoretical cost, but linear actual cost *)
  let t = test t 0 in
  let t = test t 1 in
  let t = test t 2 in
  let t = test t 3 in
  let t = test t 4 in
  let t = test t 8 in
  let t = test t 16 in
  [%expect {|
    (exp 0 = 1 , cost((theoretical_num_apps 46)(theoretical_num_allocs 17)(num_apps 38)(num_allocs 11)(num_cache_hits 5)))
    (exp 1 = 2 , cost((theoretical_num_apps 143)(theoretical_num_allocs 53)(num_apps 12)(num_allocs 2)(num_cache_hits 5)))
    (exp 2 = 4 , cost((theoretical_num_apps 337)(theoretical_num_allocs 125)(num_apps 13)(num_allocs 3)(num_cache_hits 4)))
    (exp 3 = 8 , cost((theoretical_num_apps 725)(theoretical_num_allocs 269)(num_apps 14)(num_allocs 4)(num_cache_hits 3)))
    (exp 4 = 16 , cost((theoretical_num_apps 1501)(theoretical_num_allocs 557)(num_apps 14)(num_allocs 4)(num_cache_hits 3)))
    (exp 8 = 256 , cost((theoretical_num_apps 24781)(theoretical_num_allocs 9197)(num_apps 56)(num_allocs 16)(num_cache_hits 9)))
    (exp 16 = 65536 , cost((theoretical_num_apps 6356941)(theoretical_num_allocs 2359277)(num_apps 112)(num_allocs 32)(num_cache_hits 17)))
    |}];
  (* caches are also tiny compared to the resulting tree, thanks to sharing *)
  print_s [%sexp "known trees", (Map.length t.decode : int)];
  print_s [%sexp "known apps", (Map.length t.cache : int)];
  [%expect {|
    ("known trees" 130)
    ("known apps" 316)
    |}]

let%expect_test "naive fib" =
  let fib_tree =
    (* Function that takes a small nat n and returns a nat that's the n-th
       fibonacci number, by naively recursing, i.e. yielding an exponential call
       pattern. Lambada syntax:
       fib = \n (fix $ \self triage 1 (\n add (self (sn_pred n)) (self n)) △) (sn_pred n) *)
    Of_sexp.t_of_sexp
      (Sexp.of_string
         "(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(()))))(()(())(()(()(()(())(()(()(()(()(()(())(()(()(()(())(())))(()(()(()(()))(()))))))(()(()(()(()(()(())(()(()(()(())(())))(()))))(()(()(()(())(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(())(()(()(()(())(())))(()))))(()(()(()(())(()(()))))(()(()(()(())(())))(())))))(()(())(()(()))))))(()(()(()(())(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(()))))(()(())(()(()(()(())(()(()(()(())(()(()(()(()(())(()(())(()(()(()))(()))))(()))(())))))(()(()(()(()(()(())(()(()(()(())(()(()(()(())(())))(()))))(()(())))))(()(()(()(())(()(()(()(()(()(())(()(()(()(())(())))(()))))(()(()(()(())(()(()))))(()(()(()(())(())))(())))))(()(())(()(()))))))(()(()(()(())(()(()(()(())(()(()(()(())(()(()(()(())(())))(()))))(()(()))))))))(()(()(()(())(()(()(()(())(()(()(())(()))))))))(()(()(()(())(()(()(()(())(()(()(()(()(()(())(()(()(()(())(()(()(()(())(())))(()))))(()(())))))(()))))))(()(())))))))))))(()(())(()(()(()(())(()(()(()(()(()(())(()(()(())(())))))(()(()(()(()(()(()(()))(()(())(())))(()(())(()(()(()(()(()(())(())))(()(()(()(()))(()(())(())))(()))))(()(()(()(())))(())))))(()))(()(())(()(()(()(()(()(()(()(())(())))(()(()(()(()))(()(())(())))(()))))(()(()(()(())))(())))(()(())(()(()(()))(()(())(()(()))))))(()))))(())))))))(()(()))))))))(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(())))))))(()(())))))))))))(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(()))))(()(())(()(()(()(())(()(()(()(())(()(()(()(()(()(())(()(()(()(())(())))(()))))(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(()))))(()(())(()(()(()(())(()(()(()(())(()(()(())(())))))(()(()(()(())(()(()(()(()(()(())(()(()(()(())(()(()(()(())(())))(()))))(()(())))))(()(()(()(())(())))(()(()))))))))(()(()))))))(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(())))))))(()(())))))))(()))(()(()(()(())(()(()))))(()(()(()(())(()(()(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(()))))(()(())(()(()(()(())(()(()(()(())(()(()(())(())))))(()(()(()(())(()(()(()(()(()(())(()(()(()(())(()(()(()(())(())))(()))))(()(())))))(()(()(()(())(())))(()(()(()))(()(())(())))))))))(()(()))))))(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(())))))))(()(()))))))(())))))(()(()(()(())(()(()(()(())(()(())))))))(()(()(()(())(()(()(()(())(()))))))(())))))))))))(()(()(()(())(()(()(()(()(()(())))(()))(())))))(()(()(()(())(()(()(()(())(()(()(()(()(()(())(()(()(()(())(())))(()))))(()(()(()(())(()(()(()(())(()(()(()(()(()(())(()(()(()(())(()))))))(())))(()(())(()))))))))(())))))))(()(())))))(()(()(()(())(()(()))))))))))(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(())))))))(()(())))))))))(()(())(()))))))))(()(()(()(()(()(())(()(()(()(())(())))(()))))(()(()))))(()(())(()(()(())(()(()(()(())))(())))(())))))))(()(()(()(())))(())))))(()(())(())))))(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(())))))))(()(()))))))))(()(()(())(()(()(()(())))(())))(())))")
  in
  let t = empty in
  let t, fib_id = encode t fib_tree in
  let test t n =
    let t, n_id = encode t (Marshal.tree_of_small_int n) in
    let t, { App_res.id = res_id; cost } = apply t fib_id n_id in
    print_s ~mach:()
      [%sexp
        "fib",
        (n : int),
        "=",
        (decode t res_id |> Marshal.int_of_tree : int),
        ",",
        "cost",
        (cost : Cost.t)];
    t
  in
  (* once again we expect linear actual cost *)
  let t = test t 8 in
  let t = test t 16 in
  let t = test t 32 in
  let t = test t 64 in
  [%expect
    {|
    (fib 8 = 34 , cost((theoretical_num_apps 24618)(theoretical_num_allocs 8380)(num_apps 1297)(num_allocs 321)(num_cache_hits 285)))
    (fib 16 = 1597 , cost((theoretical_num_apps 1240125)(theoretical_num_allocs 422216)(num_apps 2903)(num_allocs 773)(num_cache_hits 580)))
    (fib 32 = 3524578 , cost((theoretical_num_apps 2743918347)(theoretical_num_allocs 934204111)(num_apps 13933)(num_allocs 3787)(num_cache_hits 2504)))
    (fib 64 = 17167680177565 , cost((theoretical_num_apps 13365234260278392)(theoretical_num_allocs 4550374781676206)(num_apps 65378)(num_allocs 17886)(num_cache_hits 11382)))
    |}];
  (* computing (sub-)results again should be close to free *)
  let t = test t 64 in
  let t = test t 63 in
  [%expect
    {|
    (fib 64 = 17167680177565 , cost((theoretical_num_apps 13365234260278392)(theoretical_num_allocs 4550374781676206)(num_apps 0)(num_allocs 0)(num_cache_hits 1)))
    (fib 63 = 10610209857723 , cost((theoretical_num_apps 8260169040452570)(theoretical_num_allocs 2812286276624898)(num_apps 5)(num_allocs 0)(num_cache_hits 3)))
    |}];
  print_s [%sexp "known trees", (Map.length t.decode : int)];
  print_s [%sexp "known apps", (Map.length t.cache : int)];
  [%expect
    {|
    ("known trees" 23083)
    ("known apps" 83831)
    |}]

let apply a b =
  let t = empty in
  let t, a = encode t a in
  let t, b = encode t b in
  let t, { App_res.id; cost = _ } = apply t a b in
  decode t id
