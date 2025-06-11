(** Converts a tree to a sexp. There are various ways to do this. A fairly
    compact way would be to directly mirror the unlabeled binary tree with sexp
    lists, i.e. map [Leaf] to [()], [Stem u] to [(u)], and [Fork (u, v)] to
    [(u v)]. However, there is a more human-friendly way: We can make it so
    [(f x y)] represents [f] applied to [x] and [y], which is not the case for
    the above mapping. We still encode [Leaf] as [()] but interpret non-empty
    sexp lists as function application, so [Stem u] maps to [(() u)] and
    [Fork (u, v)] maps to [(() u v)]. *)

open Core
open Tree

val sexp_of_t : t -> Sexp.t
