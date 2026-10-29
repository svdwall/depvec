import Depvec.Unrolling
import Depvec.Run

/-!
# The trace of a path is a run
-/

namespace Depvec

variable {P : Program}

/-- Every block of a path is a block of the program. -/
theorem IsPath.mem_blocks {π : List BlockId} (hP : P.WellFormed) (hπ : IsPath P π) :
    ∀ (k : Nat) (b : BlockId), π[k]? = some b → b ∈ P.blocks := by
  intro k
  induction k with
  | zero =>
    intro b hb
    rw [← List.head?_eq_getElem?, hπ.1] at hb
    cases hb
    exact hP.entry_mem
  | succ k ih =>
    intro b hb
    have hk : k < π.length := by
      have := (List.getElem?_eq_some_iff.1 hb).1
      omega
    have hb₀ := List.getElem?_eq_getElem hk
    exact hP.succ_mem _ (ih _ hb₀) _ (hπ.2 k _ _ hb₀ hb)

/-- Consecutive events of the blocks of a path are CFG steps. -/
theorem flatMap_step (hP : P.WellFormed) :
    ∀ (L : List BlockId), (∀ (k : Nat) (b : BlockId), L[k]? = some b → b ∈ P.blocks) →
      (∀ (k : Nat) (b b' : BlockId), L[k]? = some b → L[k + 1]? = some b' → b' ∈ P.succ b) →
      ∀ (k : Nat) (x y : Node), (L.flatMap (fun b => (P.code b).map Node.instr))[k]? = some x →
        (L.flatMap (fun b => (P.code b).map Node.instr))[k + 1]? = some y → P.Step x y := by
  intro L
  induction L with
  | nil => intro _ _ k x y hx; simp at hx
  | cons b rest ih =>
    intro hmem hsucc k x y hx hy
    have hb : b ∈ P.blocks := hmem 0 b rfl
    have hc : P.code b ≠ [] := hP.code_ne b hb
    have hlen : 0 < (P.code b).length := List.length_pos_iff.2 hc
    rw [List.flatMap_cons] at hx hy
    rcases Nat.lt_or_ge (k + 1) (P.code b).length with hk | hk
    · -- Both events are in block `b`.
      rw [List.getElem?_append_left (by simp; omega), List.getElem?_map] at hx hy
      obtain ⟨x', hx', rfl⟩ := Option.map_eq_some_iff.1 hx
      obtain ⟨y', hy', rfl⟩ := Option.map_eq_some_iff.1 hy
      exact .next hb hx' hy'
    · rcases Nat.lt_or_ge k (P.code b).length with hk' | hk'
      · -- `x` ends block `b` and `y` starts the next block.
        have hkeq : k = (P.code b).length - 1 := by omega
        rw [List.getElem?_append_left (by simp; omega), List.getElem?_map] at hx
        rw [List.getElem?_append_right (by simp; omega), List.length_map] at hy
        rw [show k + 1 - (P.code b).length = 0 by omega] at hy
        obtain ⟨x', hx', rfl⟩ := Option.map_eq_some_iff.1 hx
        match rest, hy with
        | [], hy => simp at hy
        | b' :: rest', hy =>
          rw [List.flatMap_cons] at hy
          have hb' : b' ∈ P.blocks := hmem 1 b' rfl
          have hlen' : 0 < (P.code b').length := List.length_pos_iff.2 (hP.code_ne b' hb')
          rw [List.getElem?_append_left (by simp; omega), List.getElem?_map] at hy
          obtain ⟨y', hy', rfl⟩ := Option.map_eq_some_iff.1 hy
          refine .jump hb (hsucc 0 b b' rfl rfl) ?_ ?_
          · unfold Program.IsTerm
            rw [List.getLast?_eq_getElem?, ← hkeq]
            exact hx'
          · unfold Program.IsFirst
            rw [List.head?_eq_getElem?]
            exact hy'
      · -- Both events are in later blocks.
        rw [List.getElem?_append_right (by simp; omega), List.length_map] at hx hy
        rw [show k + 1 - (P.code b).length = (k - (P.code b).length) + 1 by omega] at hy
        exact ih (fun k b h => hmem (k + 1) b h)
          (fun k b b' h h' => hsucc (k + 1) b b' h h') _ x y hx hy

/-- The trace of a path is a run. -/
theorem trace_run {π : List BlockId} (hP : P.WellFormed) (hπ : IsPath P π) :
    Run P (nodeAt P π) where
  init := rfl
  pre := by
    intro p q hqp ⟨x, hx⟩
    have hp := (List.getElem?_eq_some_iff.1 hx).1
    exact ⟨_, List.getElem?_eq_getElem (by omega)⟩
  step := by
    intro p x y hx hy
    unfold nodeAt trace at hx hy
    match p with
    | 0 =>
      simp only [List.getElem?_cons_zero, Option.some.injEq] at hx
      subst hx
      rw [List.getElem?_cons_succ] at hy
      match π, hπ, hy with
      | [], _, hy => simp at hy
      | b :: rest, hπ, hy =>
        have hb : b = P.entry := by
          have := hπ.1
          simp at this
          exact this
        subst hb
        rw [List.flatMap_cons] at hy
        have hlen : 0 < (P.code P.entry).length :=
          List.length_pos_iff.2 (hP.code_ne _ hP.entry_mem)
        rw [List.getElem?_append_left (by simp; omega), List.getElem?_map] at hy
        obtain ⟨y', hy', rfl⟩ := Option.map_eq_some_iff.1 hy
        refine .start ?_
        unfold Program.IsFirst
        rw [List.head?_eq_getElem?]
        exact hy'
    | p + 1 =>
      rw [List.getElem?_cons_succ] at hx hy
      exact flatMap_step hP π (hπ.mem_blocks hP) hπ.2 p x y hx hy

end Depvec
