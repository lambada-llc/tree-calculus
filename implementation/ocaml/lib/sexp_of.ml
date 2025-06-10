open Core
open Tree

let node_sexp = Sexp.List []

let rec sexp_of_t = function
  | Leaf -> node_sexp
  | Stem t -> List [ node_sexp; sexp_of_t t ]
  | Fork (t1, t2) -> List [ node_sexp; sexp_of_t t1; sexp_of_t t2 ]
