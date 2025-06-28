open Core
open Tree

module Id = struct
  include Int
end

module Cost = struct
  include Int
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
  ({ t with decode }, { App_res.id; cost = 0 })

let empty =
  {
    decode = Map.singleton (module Id) 0 Shallow_node.Leaf;
    cache = Map.empty (module App);
  }

let rec apply t a b =
  let open Shallow_node in
  match Map.find t.cache (a, b) with
  | Some res -> (t, res)
  | None ->
      let t, res =
        match Map.find_exn t.decode a with
        | Leaf -> alloc t (Stem b)
        | Stem a -> alloc t (Fork (a, b))
        | Fork (x, y) -> (
            match Map.find_exn t.decode x with
            | Leaf -> (t, { App_res.id = y; cost = 0 })
            | Stem a1 ->
                let t, { App_res.id = a1; cost = c1 } = apply t a1 b in
                let t, { App_res.id = y; cost = c2 } = apply t y b in
                let t, { App_res.id = res; cost = c3 } = apply t a1 y in
                (t, { App_res.id = res; cost = c1 + c2 + c3 })
            | Fork (a1, a2) -> (
                match Map.find_exn t.decode b with
                | Leaf -> (t, { App_res.id = a1; cost = 0 })
                | Stem u ->
                    let t, res = apply t a2 u in
                    (t, res)
                | Fork (u, v) ->
                    let t, { App_res.id = res; cost = c1 } = apply t y u in
                    let t, { App_res.id = res; cost = c2 } = apply t res v in
                    (t, { App_res.id = res; cost = c1 + c2 })))
      in
      let res = { res with cost = res.cost + 1 } in
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
  print_s [%sexp (t : t)];
  [%expect
    {|
    ((decode
      ((0 Leaf) (1 (Stem 0)) (2 (Fork 0 0)) (3 (Stem 1)) (4 (Fork 1 2))
       (5 (Stem 4)) (6 (Fork 4 0))))
     (cache
      (((0 0) ((id 1) (cost 1))) ((0 1) ((id 3) (cost 1)))
       ((0 4) ((id 5) (cost 1))) ((1 0) ((id 2) (cost 1)))
       ((2 0) ((id 0) (cost 1))) ((3 2) ((id 4) (cost 1)))
       ((5 0) ((id 6) (cost 1))) ((6 0) ((id 1) (cost 1)))
       ((6 1) ((id 0) (cost 2))))))
    |}]

let%expect_test "naive fib" =
  let fib_tree =
    Of_sexp.t_of_sexp
      (Sexp.of_string
         "(()(()(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(()))))(()(())(()(()(()(())(()(()(()(()(()(())(()(()(()(())(())))(()))))(()(()(()(())(()(()(()(())(()(()(()(()(()(()(()(())(()(()(()(())(()))))))(())))(()(())(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(()))))(()(())(()(()(()(())(()(()(()(())(()(()(()(()(()))(()))(())))))(()(()(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(())(()(()(()(())(())))(()))))(()(()(()(())(()(()))))(()(()(()(())(())))(())))))(()(())(()(()))))))(()(()(()(())(()(()(()(())(()(()(()(()(()(())(()(()(()(())(()))))))(()))))))(()(())))))(()(())))))(()(())(())))))(()(()(()(())(()(())))))))(()(())(()(()(()))))))))(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(())))))))(()(()))))))))))(()(())(()))))))))(()(()(()(())(()(()(()(())(()(()(()(()(()(())(()(()(()(())(()(()(()(())(())))(()))))(()(())))))(()(()(()(())(()(()(()(())(()(()(()(()(()(())))(()))))))(()(())))))(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(()))))(()(())(()(()(()(())(()(()(()(())(()(()(())(())))))(()(()(()(())(()(()(()(()(()(()(()(())(()(()(()(())(()))))))(())))(()(())(()(())(()(()(())(()))(()(()(()(())(()(()(()(())(()(())))))))(())))))))(()(())(())))))(()(()(()(())(()(()(()))))))))))(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(())))))))(()(()))))))))))))(()(())))))(()(()(()(()(()(())(()(()(()(())(()(()(()(())(())))(()))))(()(())))))(()(()(()(())(())))(()))))(()(()(()(())(()(()(()(()(()(())(()(()(()(())(())))(()))))(()(()(()(())(()(()))))(()(()(()(())(())))(())))))(()(())(()(()))))))(()(()(()(()(()(())(()(()(()(())(())))(()))))(()(()))))(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(()))))(()(())(()(()(()(())(()(()(()(())(()(()(())(())))))(()(()(()(())(()(()(()(()(()(()(()(())(()(()(()(())(()))))))(())))(()(())(()(())(()(()(())(()))(()(()(()(())(()(()(()(())(()(())))))))(())))))))(()(())(())))))(()(()(()(())(()(()(()))))))))))(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(())))))))(()(()))))))))))))))(()(())(()(()(()(()))(()(())(())))(()(())(()(())(()))))))))(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(())))))))(()(()))))))))(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(()))))(()(())(()(()(()(())(()(()(()(())(()(()(())(())))))(()(()(()(())(()(()(()(()(()(()(()(())(()(()(()(())(()))))))(())))(()(())(()(())(()(()(())(()))(()(()(()(())(()(()(()(())(()(())))))))(())))))))(()(())(())))))(()(()(()(())(()(()(()))))))))))(()(()(()(())(()(()(()(()(()(())(()(()(()(()(()(()(()(())))(())))(()(()(()(())))(())))))))(()(())))))))(()(()))))))))(()(())(())))")
  in
  let t = empty in
  let t, fib_id = encode t fib_tree in
  let test t n =
    let t, n_id = encode t (Marshal.tree_of_int n) in
    let t, { App_res.id = res_id; cost = _ } = apply t fib_id n_id in
    print_s
      [%sexp
        "fib",
        (decode t n_id |> Marshal.int_of_tree : int),
        "=",
        (decode t res_id |> Marshal.int_of_tree : int)];
    t
  in
  let t = test t 4 in
  let t = test t 8 in
  let t = test t 12 in
  let t = test t 16 in
  [%expect {|
    (fib 4 = 5)
    (fib 8 = 34)
    (fib 12 = 233)
    (fib 16 = 1597)
    |}];
  print_s [%sexp "known trees", (Map.length t.decode : int)];
  print_s [%sexp "known apps", (Map.length t.cache : int)];
  print_s
    [%sexp
      "max app cost",
      (Map.data t.cache
       |> List.map ~f:(fun { App_res.cost; _ } -> cost)
       |> List.max_elt ~compare:Int.compare
       |> Option.value_exn
        : int)];
  [%expect
    {|
    ("known trees" 8304)
    ("known apps" 38878)
    ("max app cost" 953582)
    |}]
