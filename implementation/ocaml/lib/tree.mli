type t = Leaf | Stem of t | Fork of t * t

val apply : t -> t -> t
