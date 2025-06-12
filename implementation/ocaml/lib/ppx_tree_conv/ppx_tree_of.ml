open Core
open Ppxlib

let fmt : _ format = "tree_of_%s"

let rec tree_of_type ty =
  let { ptyp_desc; ptyp_loc = loc; ptyp_loc_stack = _; ptyp_attributes = _ } =
    ty
  in
  match ptyp_desc with
  | Ptyp_constr (id, args) ->
      let args = List.map args ~f:tree_of_type in
      Ast_builder.Default.type_constr_conv ~loc ~f:(Printf.sprintf fmt) id args
  | _ ->
      Location.raise_errorf ~loc "Type not supported: %a" Pprintast.core_type ty

let () =
  let name = "tree_of" in
  Driver.register_transformation
    ~rules:
      [
        Context_free.Rule.extension
          (Extension.V3.declare name Extension.Context.expression
             Ast_pattern.(ptyp __)
             (fun ~ctxt:_ ty -> tree_of_type ty));
      ]
    name
