import TreeCalculus.Check
import TreeCalculus.Runtime
import TreeCalculus.Jets.Trees

/-!
# Jets on real trees

Each tree in `Jets/Trees.lean` is an eager normal form the toolchain computed (`jets/gen.mjs`):
LambAda compiled by `lambada emit`, or a symbol of `compile_file.dag` itself, normalized by
`runner`. Each `…_check` is one kernel evaluation (`decide +kernel`, no `native_decide`);
everything else about the tree follows from it by the checker's theorems.
-/

namespace TreeCalculus.Jets

/-! ## Predicates: membership in a set of trees, against every tree -/

/-- `equal_const 10`, written in LambAda. -/
theorem eqConstNewline_check : checkMember eqConstNewline [newline] 1000 = true := by
  decide +kernel

theorem eqConstNewline_spec (c : Tree) :
    Run eqConstNewline c (if c = newline then .true else .false) :=
  checkMember_single eqConstNewline_check c

/-- The compiler's `_is_hash`, which is `equal_const '#'`. -/
theorem isHash_check : checkMember isHash [hash] 1000 = true := by decide +kernel

theorem isHash_spec (c : Tree) : Run isHash c (if c = hash then .true else .false) :=
  checkMember_single isHash_check c

/-- `Char.is_newline`, which is `Fn.p_or (equal_const 10) (equal_const 13)`. -/
theorem isNewline_check : checkMember isNewline [newline, carriageReturn] 1000 = true := by
  decide +kernel

theorem isNewline_spec (c : Tree) :
    Run isNewline c (if c ∈ [newline, carriageReturn] then .true else .false) :=
  checkMember_sound isNewline_check c

/-! ## Loops: dropping a list through a separator -/

/-- `fix $ \self List.match [] (\h Bool.match self id (equal_const 10 h))`, a skip-to-newline
loop written in LambAda. -/
theorem skipLineFix_check : checkDropThrough skipLineFix [newline] 1000 = true := by
  decide +kernel

theorem skipLineFix_spec (h : IsList xs) : Run skipLineFix xs (dropThrough [newline] xs) :=
  checkDropThrough_list skipLineFix_check h

/-- The compiler's `skip_line`, which `statements` reads every line past its first character
with: a comment line and a build's `# = …` results whole, and a code line before `_piece`
copies its text out. This is the loop a source's comments cost. -/
theorem skipLine_check : checkDropThrough skipLine [newline] 1000 = true := by decide +kernel

theorem skipLine_spec (h : IsList xs) : Run skipLine xs (dropThrough [newline] xs) :=
  checkDropThrough_list skipLine_check h

/-- What a native scan may do: skip any tree but a newline, then hand the rest back to the
tree… -/
theorem skipLine_skip (hc : c ∉ [newline]) (ht : Run skipLine t r) :
    Run skipLine (.fork c t) r :=
  checkDropThrough_skip skipLine_check hc ht

/-- …or, at a newline, answer with the rest of the list itself. -/
theorem skipLine_hit (t : Tree) : Run skipLine (.fork newline t) t :=
  checkDropThrough_hit skipLine_check (List.mem_singleton_self _) t

/-- The compiler's `_skip_comment`, which the tokenizer drops a comment inside a statement with.
A comment line of its own never reaches it — `statements` has left those out — so on a source
it runs rarely. A comment ends at a newline or at a carriage return (`Char.is_newline`), so it
takes both. -/
theorem skipComment_check :
    checkDropThrough skipComment [newline, carriageReturn] 1000 = true := by decide +kernel

theorem skipComment_spec (h : IsList xs) :
    Run skipComment xs (dropThrough [newline, carriageReturn] xs) :=
  checkDropThrough_list skipComment_check h

/-! ## A runtime taking them -/

/-- The compiler's jets, as transitions. -/
def compilerJets (s t : State Tree) : Prop :=
  DropJet skipLine [newline] s t ∨ DropJet skipComment [newline, carriageReturn] s t ∨
  MemberJet isNewline [newline, carriageReturn] s t ∨ MemberJet isHash [hash] s t

/-- A runtime that memoizes, and takes the compiler's jets wherever it likes, returns what the
reduction rules say. -/
theorem compilerJets_sound (hm : m.Sound)
    (h : Star (RStep compilerJets) ⟨.reduce a b [], [], m⟩ ⟨.dispatch r [], [], m'⟩) :
    Run a b r ∧ m'.Sound :=
  runtime_sound (fun _ _ => fun
    | .inl h => DropJet.reaches skipLine_check h
    | .inr (.inl h) => DropJet.reaches skipComment_check h
    | .inr (.inr (.inl h)) => MemberJet.reaches isNewline_check h
    | .inr (.inr (.inr h)) => MemberJet.reaches isHash_check h) hm h

/-! ## What the checkers refuse -/

example : checkMember isHash [newline] 1000 = false := by decide +kernel
example : checkMember isNewline [newline] 1000 = false := by decide +kernel
example : checkDropThrough skipLineFix [carriageReturn] 1000 = false := by decide +kernel
/-- `skip_line` ends a line at a newline only: a carriage return is a character like any other. -/
example : checkDropThrough skipLine [newline, carriageReturn] 1000 = false := by decide +kernel
/-- A jet that skipped `_skip_comment`'s argument to the next newline would be wrong: a carriage
return ends the comment first. -/
example : checkDropThrough skipComment [newline] 1000 = false := by decide +kernel

end TreeCalculus.Jets
