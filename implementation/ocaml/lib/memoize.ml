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

let%expect_test "naive fib" =
  let rec tree_of_small_int = function
    | 0 -> Leaf
    | n when n > 0 -> Stem (tree_of_small_int (n - 1))
    | _ -> failwith "Negative integers are not supported"
  in
  let fib_tree =
    Of_sexp.t_of_sexp
      (Sexp.of_string
         "(()(()(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(()))))(()(())(()(()(()(())(()(()(()(()(()(())(()(()(()(())(())))(()))))(()(()(()(())(()(()(()(())(()(()(()(()(()(()(()(())(()(()(()(())(()))))))(())))(()(())(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(()))))(()(())(()(()(()(())(()(()(()(())(()(()(()(()(()))(()))(())))))(()(()(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(())(()(()(()(())(())))(()))))(()(()(()(())(()(()))))(()(()(()(())(())))(())))))(()(())(()(()))))))(()(()(()(())(()(()(()(())(()(()(()(()(()(())(()(()(()(())(()))))))(()))))))(()(())))))(()(())))))(()(())(())))))(()(()(()(())(()(())))))))(()(())(()(()(()))))))))(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(())))))))(()(()))))))))))(()(())(()))))))))(()(()(()(())(()(()(()(())(()(()(()(()(()(())(()(()(()(())(()(()(()(())(())))(()))))(()(())))))(()(()(()(())(()(()(()(())(()(()(()(()(()(())))(()))))))(()(())))))(()(()(())(()(()(()(())))(())))(()))))))))(()(())))))(()(()(()(()(()(())(()(()(()(())(()(()(()(())(())))(()))))(()(())))))(()(()(()(())(())))(()))))(()(()(()(())(()(()(()(()(()(())(()(()(()(())(())))(()))))(()(()(()(())(()(()))))(()(()(()(())(())))(())))))(()(())(()(()))))))(()(()(()(()(()(())(()(()(()(())(())))(()))))(()(()))))(()(())(()(()(())(()(()(()(())))(())))(()))))))))))(()(())(()(()(()(()))(()(())(())))(()))))))(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(())))))))(()(()))))))))(()(()(())(()(()(()(())))(())))(()))))(()(())(())))")
  in
  let t = empty in
  let t, fib_id = encode t fib_tree in
  let test t n =
    let t, n_id = encode t (tree_of_small_int n) in
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
  let t = test t 0 in
  let t = test t 1 in
  let t = test t 2 in
  let t = test t 3 in
  let t = test t 4 in
  let t = test t 8 in
  let t = test t 16 in
  let t = test t 15 in
  let t = test t 24 in
  let t = test t 23 in
  [%expect
    {|
    (fib 0 = 1 , cost((theoretical_num_apps 203)(theoretical_num_allocs 67)(num_apps 171)(num_allocs 45)(num_cache_hits 23)))
    (fib 1 = 1 , cost((theoretical_num_apps 207)(theoretical_num_allocs 69)(num_apps 6)(num_allocs 0)(num_cache_hits 5)))
    (fib 2 = 2 , cost((theoretical_num_apps 633)(theoretical_num_allocs 211)(num_apps 51)(num_allocs 6)(num_cache_hits 18)))
    (fib 3 = 3 , cost((theoretical_num_apps 984)(theoretical_num_allocs 329)(num_apps 81)(num_allocs 18)(num_cache_hits 17)))
    (fib 4 = 5 , cost((theoretical_num_apps 1842)(theoretical_num_allocs 616)(num_apps 109)(num_allocs 25)(num_cache_hits 24)))
    (fib 8 = 34 , cost((theoretical_num_apps 14320)(theoretical_num_allocs 4796)(num_apps 875)(num_allocs 192)(num_cache_hits 209)))
    (fib 16 = 1597 , cost((theoretical_num_apps 686733)(theoretical_num_allocs 230097)(num_apps 37392)(num_allocs 7868)(num_cache_hits 10019)))
    (fib 15 = 987 , cost((theoretical_num_apps 424066)(theoretical_num_allocs 142087)(num_apps 81)(num_allocs 0)(num_cache_hits 67)))
    (fib 24 = 75025 , cost((theoretical_num_apps 32295510)(theoretical_num_allocs 10821084)(num_apps 1739177)(num_allocs 365190)(num_cache_hits 468660)))
    (fib 23 = 46368 , cost((theoretical_num_apps 19959472)(theoretical_num_allocs 6687712)(num_apps 55)(num_allocs 0)(num_cache_hits 45)))
    |}];
  print_s [%sexp "known trees", (Map.length t.decode : int)];
  print_s [%sexp "known apps", (Map.length t.cache : int)];
  print_s
    [%sexp
      "max app cost",
      (Map.data t.cache
       |> List.map ~f:(fun { App_res.cost; _ } ->
              cost.Cost.theoretical_num_apps)
       |> List.max_elt ~compare:Int.compare
       |> Option.value_exn
        : int)];
  [%expect
    {|
    ("known trees" 373512)
    ("known apps" 1778165)
    ("max app cost" 32295510)
    |}]

let apply a b =
  let t = empty in
  let t, a = encode t a in
  let t, b = encode t b in
  let t, { App_res.id; cost = _ } = apply t a b in
  decode t id
