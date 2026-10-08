import TreeCalculus.Progress
import Cpp.Jets

/-! # Progress for the C++ runtime's jet (`Cpp/Jets.lean`'s `skipLine`) -/

namespace Cpp.Jets
open TreeCalculus

/-- The jet `implementation/cpp/jets.hpp` takes makes progress in every transition. -/
theorem skipLine_plus : DropJet skipLine [newline] s t → Plus s t := DropJet.plus skipLine_check

/-- Under any policy, the runtime with it returns `r` iff the machine does. -/
theorem skipLine_returns_iff (pol : Policy (DropJet skipLine [newline]) P a b m) (hm : m.Sound) :
    (∃ m', Star P (RState.init a b m) ⟨.dispatch r [], [], m'⟩) ↔ Run a b r :=
  runtime_returns_iff pol (fun _ _ h => (skipLine_plus h).reaches) hm

/-- And `MemoPolicy` with it is such a policy. -/
theorem skipLine_memoPolicy_returns_iff (hm : m.Sound) :
    (∃ m', Star (MemoPolicy (DropJet skipLine [newline])) (RState.init a b m)
      ⟨.dispatch r [], [], m'⟩) ↔ Run a b r :=
  memoPolicy_returns_iff (fun _ _ => skipLine_plus) hm

end Cpp.Jets
