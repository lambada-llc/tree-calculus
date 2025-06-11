type t =
  | Ref of string (* variables are indexed by strings *)
  | Node
  | App of (t * t)

val triage : t -> t -> t -> t
val ( * ) : t -> t -> t
val ( ^ ) : string -> t -> t
val to_tree : t -> Tree.t
val of_tree : Tree.t -> t
