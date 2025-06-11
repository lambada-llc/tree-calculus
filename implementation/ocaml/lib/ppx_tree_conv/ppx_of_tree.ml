open Core
open Ppxlib

let fmt : _ format = "%s_of_tree"

let rec type_of_tree ty =
  let { ptyp_desc; ptyp_loc = loc; ptyp_loc_stack = _; ptyp_attributes = _ } =
    ty
  in
  match ptyp_desc with
  | Ptyp_constr (id, args) ->
      let args = List.map args ~f:type_of_tree in
      Ast_builder.Default.type_constr_conv ~loc ~f:(Printf.sprintf fmt) id args
  | _ ->
      let ext =
        Location.error_extensionf ~loc "Type not supported: %a"
          Pprintast.core_type ty
      in
      Ast_builder.Default.pexp_extension ~loc ext

let () =
  let name = "of_tree" in
  Driver.register_transformation
    ~rules:
      [
        Context_free.Rule.extension
          (Extension.V3.declare name Extension.Context.expression
             Ast_pattern.(ptyp __)
             (fun ~ctxt:_ ty -> type_of_tree ty));
      ]
    name
