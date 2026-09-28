import TreeCalculus.Tree

/-!
# The eager machine: `EagerGraphNilMmap32::apply`, rule by rule

`apply(a, b)` in `implementation/cpp/eager-graph-nil-mmap-32.hpp` is a loop over two labels,
`reduce:` and `dispatch:`, with its continuations as `Frame`s on a heap stack. `step` below is
one `goto` of that loop, its branches in the C++'s order and quoting its comments; `Run a b r`
is one call of `apply` returning `r`.

What the C++ does besides, and why none of it changes a result:

* **Hash-consing** (`stem()`/`fork()` intern): a node index *is* a tree — exact, not a hint —
  so where the C++ compares indices, this compares trees.
* **Collection** (`collect_if_over_budget`): frees only what no root, frame or operand reaches,
  and nothing moves, so every index the loop holds still names the tree it named.
* **`MEMOIZE` frames and the memo**: a cache, put back in `Memo.lean`, whose `memo_sound` proves
  that a machine free to hit, fill and forget its memo any way at all returns what `Run` says,
  and only ever holds entries `Run` agrees with. That the entries' keys — indices — mean trees
  is hash-consing again, plus the collector dropping every entry one of whose three nodes it
  frees, so no index is reused under a stale entry.
-/

namespace TreeCalculus

/-- `Frame`, minus `MEMOIZE`. Parameterized by what a tree is, so the symbolic machine
(`Symbolic.lean`) runs on the same states. -/
inductive Frame (α : Type) where
  /-- `APPLY_TO(arg)`: when the current reduction lands its result `r`, begin `apply(r, arg)`. -/
  | applyTo (arg : α)
  /-- `COMPUTE_AND_APPLY(fn, arg)`: when it lands `r`, push `APPLY_TO(r)` and begin
  `apply(fn, arg)` — the `apply(apply(fn, arg), r)` shape rule (2) reduces to. -/
  | computeAndApply (fn arg : α)

/-- A label of `apply`'s loop, with the frames above `base`, top first. -/
inductive State (α : Type) where
  /-- `reduce:` — evaluate `apply(a, b)`. -/
  | reduce (a b : α) (k : List (Frame α))
  /-- `dispatch:` — feed `result` to the pending continuation. -/
  | dispatch (r : α) (k : List (Frame α))

def Frame.map (f : α → β) : Frame α → Frame β
  | .applyTo e => .applyTo (f e)
  | .computeAndApply g e => .computeAndApply (f g) (f e)

def State.map (f : α → β) : State α → State β
  | .reduce a b k => .reduce (f a) (f b) (k.map (·.map f))
  | .dispatch r k => .dispatch (f r) (k.map (·.map f))

/-- One `goto`. `none` is `return result`: `dispatch` found the stack back at `base`. -/
def step : State Tree → Option (State Tree)
  | .reduce a b k => some <| match a with
    | .leaf => .dispatch (.stem b) k                   -- apply(△, b) = △b
    | .stem u => .dispatch (.fork u b) k               -- apply(△u, b) = △ub
    | .fork u y => match u with                        -- a = fork(u, y)
      | .leaf => .dispatch y k                         -- apply(△△y, b) = y
      | .stem u' => .reduce y b (.computeAndApply u' b :: k)
                                   -- apply(△(△u')y, b) = apply(apply(u', b), apply(y, b))
      | .fork w x => match b with                      -- apply(△(△wx)y, b) — triage on b
        | .leaf => .dispatch w k                       --   b = △:  w
        | .stem d => .reduce x d k                     --   b = △d: apply(x, d)
        | .fork d e => .reduce y d (.applyTo e :: k)   --   b = △de: apply(apply(y, d), e)
  | .dispatch _ [] => none
  | .dispatch r (.applyTo arg :: k) => some (.reduce r arg k)
  | .dispatch r (.computeAndApply fn arg :: k) => some (.reduce fn arg (.applyTo r :: k))

/-- Zero or more `r`-steps. -/
inductive Star (r : α → α → Prop) : α → α → Prop
  | refl : Star r a a
  | cons : r a b → Star r b c → Star r a c

theorem Star.trans : Star r a b → Star r b c → Star r a c := by
  intro h h'; induction h with
  | refl => exact h'
  | cons hs _ ih => exact .cons hs (ih h')

theorem Star.single (h : r a b) : Star r a b := .cons h .refl

/-- `s` runs to `t` in zero or more steps. -/
abbrev Reaches : State Tree → State Tree → Prop := Star (step · = some ·)

/-- `apply(a, b)` returns `r`: from `reduce` with nothing pushed, the loop arrives at `dispatch`
with nothing left to pop. -/
def Run (a b r : Tree) : Prop := Reaches (.reduce a b []) (.dispatch r [])

/-- A deterministic machine has at most one final state per start. -/
theorem Reaches.final (h : Reaches s t) (h' : Reaches s t') (ht : step t = none)
    (ht' : step t' = none) : t = t' := by
  induction h with
  | refl => cases h' with
    | refl => rfl
    | cons hs => simp_all
  | cons hs _ ih => cases h' with
    | refl => simp_all
    | cons hs' r => rw [hs] at hs'; cases hs'; exact ih r ht

theorem Run.det (h : Run a b r) (h' : Run a b r') : r = r' := by
  have := Reaches.final h h' rfl rfl; cases this; rfl

/-- `s` with `k` pushed underneath it. -/
def State.app (k : List (Frame α)) : State α → State α
  | .reduce a b k' => .reduce a b (k' ++ k)
  | .dispatch r k' => .dispatch r (k' ++ k)

/-- A step reads and pushes only frames above the stack it started from. -/
theorem step_app (h : step s = some s') (k) : step (s.app k) = some (s'.app k) := by
  match s, h with
  | .reduce a b _, h =>
    cases h; rcases a with _ | _ | ⟨_ | _ | _, _⟩ <;> (try rcases b with _ | _ | _) <;> rfl
  | .dispatch _ (.applyTo _ :: _), rfl => rfl
  | .dispatch _ (.computeAndApply _ _ :: _), rfl => rfl

theorem Reaches.app (h : Reaches s t) (k) : Reaches (s.app k) (t.app k) := by
  induction h with
  | refl => exact .refl
  | cons hs _ ih => exact .cons (step_app hs k) ih

/-- A run is oblivious to the stack under it — what makes a memo hit, `reduce a b k` jumping
straight to `dispatch r k`, a composite of real steps (`Memo.lean`). -/
theorem Run.frame (h : Run a b r) (k) : Reaches (.reduce a b k) (.dispatch r k) := by
  simpa [State.app] using h.app k

/-! ## Agreement with the reduction rules -/

theorem Eval.reaches (h : Eval a b r) : ∀ k, Reaches (.reduce a b k) (.dispatch r k) := by
  induction h with
  | leaf | stem | k | triageLeaf => intro k; exact .single rfl
  | s _ _ _ ih₁ ih₂ ih₃ => intro k
                           exact .cons rfl <| (ih₁ _).trans <| .cons rfl <| (ih₂ _).trans <|
                             .cons rfl (ih₃ k)
  | triageStem _ ih => intro k; exact .cons rfl (ih k)
  | triageFork _ _ ih₁ ih₂ => intro k; exact .cons rfl <| (ih₁ _).trans <| .cons rfl (ih₂ k)

/-- What a stack does to the result fed to it, in `Eval`'s terms. -/
def Cont : List (Frame Tree) → Tree → Tree → Prop
  | [], r, r' => r = r'
  | .applyTo e :: k, r, r' => ∃ v, Eval r e v ∧ Cont k v r'
  | .computeAndApply f e :: k, r, r' => ∃ v w, Eval f e v ∧ Eval v r w ∧ Cont k w r'

/-- What a state goes on to return, in `Eval`'s terms. -/
def Sem : State Tree → Tree → Prop
  | .reduce a b k, r' => ∃ v, Eval a b v ∧ Cont k v r'
  | .dispatch r k, r' => Cont k r r'

theorem step_sem (h : step s = some s') (h' : Sem s' r') : Sem s r' := by
  match s, h with
  | .reduce .leaf _ _, rfl => exact ⟨_, .leaf, h'⟩
  | .reduce (.stem _) _ _, rfl => exact ⟨_, .stem, h'⟩
  | .reduce (.fork .leaf _) _ _, rfl => exact ⟨_, .k, h'⟩
  | .reduce (.fork (.stem _) _) _ _, rfl =>
    obtain ⟨_, e₁, _, _, e₂, e₃, c⟩ := h'; exact ⟨_, .s e₁ e₂ e₃, c⟩
  | .reduce (.fork (.fork _ _) _) .leaf _, rfl => exact ⟨_, .triageLeaf, h'⟩
  | .reduce (.fork (.fork _ _) _) (.stem _) _, rfl =>
    obtain ⟨_, e, c⟩ := h'; exact ⟨_, .triageStem e, c⟩
  | .reduce (.fork (.fork _ _) _) (.fork _ _) _, rfl =>
    obtain ⟨_, e₁, _, e₂, c⟩ := h'; exact ⟨_, .triageFork e₁ e₂, c⟩
  | .dispatch _ (.applyTo _ :: _), rfl => exact h'
  | .dispatch _ (.computeAndApply _ _ :: _), rfl =>
    obtain ⟨_, e₁, _, e₂, c⟩ := h'; exact ⟨_, _, e₁, e₂, c⟩

theorem Reaches.sem (h : Reaches s (.dispatch r [])) : Sem s r := by
  generalize ht : State.dispatch r [] = t at h
  induction h with
  | refl => cases ht; rfl
  | cons hs _ ih => exact step_sem hs (ih ht)

/-- The machine computes exactly the reduction rules' big-step relation. -/
theorem run_iff_eval : Run a b r ↔ Eval a b r :=
  ⟨fun h => by obtain ⟨_, e, rfl⟩ := h.sem; exact e, fun h => h.reaches []⟩

end TreeCalculus
