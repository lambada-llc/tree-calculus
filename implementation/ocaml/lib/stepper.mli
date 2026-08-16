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

val step_peek : t -> t -> t option
(** Like [step], but rule 2 (△ (△ x) y @ z → (x z) (y z)) has the three peeks
    that avoid duplicating [z]: [x = K] yields [z] outright (y z is never
    built), and an eliminator (K f) as [x] or [y] collapses its application to
    [f]. Anything else takes the plain rule-2 reduct. Every peek is a
    contraction of the canonical normal-order sequence, so normal forms agree
    with [step]; only the step granularity differs. *)

val step_anywhere : t -> t option
(** Reduce the leftmost-outermost active application anywhere in the term by one
    step. [None] iff the term is already a value (a normal form). *)

val step_anywhere_peek : t -> t option
(** [step_anywhere] with [step_peek] as the rule set. *)

val reduce : t -> t
(** Apply [step_anywhere] until no application is active. *)

val reduce_peek : t -> t
(** Apply [step_anywhere_peek] until no application is active. *)

val nodes : t -> int
(** Number of nodes; an [App] node counts like a [Fork]. *)

val step_shrink_eager : t -> t option
(** One rewrite chosen shrink-eagerly rather than leftmost-outermost: fire the
    active application that shrinks the term most. Only rule 2 grows a term
    (by the size of the argument it duplicates); every other rule shrinks
    without duplicating, and contracting such a redex early can only make
    every later term smaller (a residual argument), so intermediate terms stay
    as small as this step relation allows. Falls back to the leftmost-outermost
    active application when nothing shrinks; that pick is not proven
    normalizing, so [step_anywhere] remains the safe default. *)
