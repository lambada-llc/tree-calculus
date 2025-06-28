module Id : sig
  type t = int
end

module Cost : sig
  type t = int
end

module App_res : sig
  type t = { id : Id.t; cost : Cost.t }
end

type t

val empty : t
val encode : t -> Tree.t -> t * Id.t
val decode : t -> Id.t -> Tree.t
val apply : t -> Id.t -> Id.t -> t * App_res.t
