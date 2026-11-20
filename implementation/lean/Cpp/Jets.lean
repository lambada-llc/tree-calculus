import TreeCalculus.Check
import TreeCalculus.Runtime
import Cpp.Jets.Trees

/-! # The jets' trees (`Jets/Trees.lean`), checked by `decide +kernel` -/

namespace Cpp.Jets
open TreeCalculus Term

/-- The compiler's `Lambada.skip_line`. -/
theorem skipLine_check : checkDropThrough skipLine [newline] 1000 = true := by decide +kernel

/-- `add [△, △△]`, which the runtime reads the pieces of `add`'s partials off (`README.md`). -/
theorem addPartial_check :
    checkRun add (.fork .leaf (.fork (.stem .leaf) .leaf)) addPartial 1000 = true := by decide +kernel

/-- `runtime_sound` for `skip_line`'s jet (`implementation/cpp/jets.hpp`); `add`'s and `mul`'s are
not proven yet (`README.md`). -/
theorem runtimeJets_sound (hm : m.Sound)
    (h : Star (RStep (DropJet skipLine [newline])) ⟨.reduce a b [], [], m⟩ ⟨.dispatch r [], [], m'⟩) :
    Apply a.toTerm b.toTerm r.toTerm ∧ m'.Sound :=
  runtime_sound (fun _ _ => DropJet.reaches skipLine_check) hm h

end Cpp.Jets
