open Tree_calculus_reference_implementation
open Tree

let _false = Leaf
let _true = Stem Leaf
let _not = Fork (Fork (_true, Fork (Leaf, _false)), Leaf)

let () =
  let open Eager_value_adt in
  (* Example: Negating booleans *)
  assert (apply _not _false = _true);
  assert (apply _not _true = _false);
  print_endline "done"
