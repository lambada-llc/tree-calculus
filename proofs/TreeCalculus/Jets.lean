import TreeCalculus.Check
import TreeCalculus.Jets.Trees

/-!
# Jets on real trees

Each tree in `Jets/Trees.lean` is an eager normal form the base toolchain computed
(`jets/gen.mjs`): LambAda compiled by `lambada emit`, or a symbol of the shipped
`compile_file.dag` itself, normalized by `runner`. Each `…_check` is one kernel evaluation
(`decide +kernel`, no `native_decide`); what the runtime may do with the tree follows from it
by the checker's soundness theorem.
-/

namespace TreeCalculus.Jets

/-! ## Predicates: `equal_const k` against every tree -/

/-- `equal_const 10`, written in LambAda. -/
theorem eqConstNewline_spec (c : Tree) :
    Run eqConstNewline c (if c = newline then .true else .false) :=
  checkEqConst_sound (n := 1000) (by decide +kernel) c

/-- The compiler's `_is_hash`, which is `equal_const '#'`. -/
theorem isHash_spec (c : Tree) : Run isHash c (if c = hash then .true else .false) :=
  checkEqConst_sound (n := 1000) (by decide +kernel) c

/-! ## Loops: dropping a list through a separator -/

/-- `fix $ \self List.match [] (\h Bool.match self id (equal_const 10 h))`, a skip-to-newline
loop written in LambAda. -/
theorem skipLine_check : checkDropThrough skipLine [newline] 1000 = true := by decide +kernel

theorem skipLine_spec (h : IsList xs) : Run skipLine xs (dropThrough [newline] xs) :=
  checkDropThrough_list skipLine_check h

/-- The compiler's `_skip_comment`, which is what reads a comment's text a character at a time.
A comment ends at a newline or at a carriage return (`Char.is_newline`), so it takes both. -/
theorem skipComment_check :
    checkDropThrough skipComment [newline, carriageReturn] 1000 = true := by decide +kernel

theorem skipComment_spec (h : IsList xs) :
    Run skipComment xs (dropThrough [newline, carriageReturn] xs) :=
  checkDropThrough_list skipComment_check h

/-- What a native scan may do: skip any character that ends no comment, then hand the rest
back to the tree… -/
theorem skipComment_skip (hc : c ∉ [newline, carriageReturn]) (ht : Run skipComment t r) :
    Run skipComment (.fork c t) r :=
  checkDropThrough_skip skipComment_check hc ht

/-- …or, at a newline or carriage return, answer with the rest of the list itself. -/
theorem skipComment_hit (hk : k ∈ [newline, carriageReturn]) (t : Tree) :
    Run skipComment (.fork k t) t :=
  checkDropThrough_hit skipComment_check hk t

/-! ## What the checkers refuse -/

example : checkEqConst isHash newline 1000 = false := by decide +kernel
example : checkEqConst eqConstNewline carriageReturn 1000 = false := by decide +kernel
example : checkDropThrough skipLine [carriageReturn] 1000 = false := by decide +kernel
/-- A jet that skipped `_skip_comment`'s argument to the next newline would be wrong: a carriage
return ends the comment first. -/
example : checkDropThrough skipComment [newline] 1000 = false := by decide +kernel

end TreeCalculus.Jets
