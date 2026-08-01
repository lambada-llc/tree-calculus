(* Stepwise reducer for tree calculus.
 *
 * [Tree.apply] reduces eagerly all the way to a value. This module instead
 * makes reduction observable one step at a time, the way the Forest DAG
 * explorer UI lets you walk a term redex by redex.
 *
 * To do so we extend the usual tree representation with an explicit [App]
 * variant that represents an outstanding application that has not been carried
 * out yet. A term without any [App] node is a value (a plain binary tree).
 *
 * TC        |  OCaml
 * ----------+-------------
 * △         |  Leaf
 * △ a       |  Stem a
 * △ a b     |  Fork (a, b)
 * a applied to b (the "@" node in reduction-rules/) |  App (a, b)
 *)
type t = Leaf | Stem of t | Fork of t * t | App of t * t

val of_tree : Tree.t -> t
(** Inject a value (no outstanding applications). *)

val to_tree : t -> Tree.t
(** Project a value back. Raises if the term still has an [App] node. *)

val step : t -> t -> t option
(** [step a b] reduces the single application of [a] to [b] by one step,
    returning [None] when that application is not (yet) active.

    These are exactly the explicit-application rules in
    [reduction-rules/README.md]. An application is inactive — [None] — when its
    arguments are not yet determined enough to pick a rule: the function [a] is
    still an [App] (not a value), or the rule-3 argument [b] is still an [App]
    (its leaf/stem/fork shape is not known yet). *)

val step_anywhere : t -> t option
(** Reduce the leftmost-outermost active application anywhere in the term by one
    step. [None] iff the term is already a value (a normal form). *)

val reduce : t -> t
(** Apply [step_anywhere] until no application is active. *)
