(** See [Sexp_of] for how trees map to sexp lists. However, when parsing sexps
    we can provide additional convenience by also giving meaning to sexp atoms:
    They are strings, so we turn them into trees via [Marshal.tree_of_string].
    As a consequence, a sexp such as [(<eval> "3 + 5")] or [(<lam> "\\x x")] or
    [(<sql> "SELECT * FROM foo")] would magically parse into trees with
    appropriate semantics, if [<...>] are trees that represent functions that
    parse these strings into respective functions. *)

open Core
open Tree

val t_of_sexp : Sexp.t -> t
