open Core
open Tree

let rec t_of_sexp = function
  | Sexp.Atom str -> Marshal.tree_of_string str
  | List l -> (
      match List.map ~f:t_of_sexp l with
      | [] -> Leaf
      | x :: xs -> List.fold_left ~init:x xs ~f:apply)
