open Tree

val bool_of_tree : t -> bool
val tree_of_bool : bool -> t
val small_int_of_tree : t -> int
val tree_of_small_int : int -> t
val option_of_tree : (t -> 'a) -> t -> 'a option
val tree_of_option : ('a -> t) -> 'a option -> t
val list_of_tree : (t -> 'a) -> t -> 'a list
val tree_of_list : ('a -> t) -> 'a list -> t
val int_of_tree : t -> int
val tree_of_int : int -> t
val char_of_tree : t -> char
val tree_of_char : char -> t
val string_of_tree : t -> string
val tree_of_string : string -> t
val t_of_tree : t -> t
val tree_of_t : t -> t
val fun_of_tree : ('arg -> t) -> (t -> 'res) -> t -> 'arg -> 'res
