import TreeCalculus.Check
import Cpp.Jets.Trees

/-! # The jets' trees (`Jets/Trees.lean`), checked by `decide +kernel` -/

namespace Cpp.Jets
open TreeCalculus

/-- The compiler's `Lambada.skip_line`. -/
theorem skipLine_check : checkDropThrough skipLine [newline] 1000 = true := by decide +kernel

end Cpp.Jets
