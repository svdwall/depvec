import Depvec.Program

/-!
# Paths and runs

General lemmas behind the soundness proof: manipulating `Reach` paths, finding
first and latest positions, and runs. A run is a sequence of nodes that starts
at `init` and takes a CFG step between consecutive positions. Unrollings and
static paths from `init` are both runs.
-/

namespace Depvec

/-! ## Paths -/

section Reach

variable {S : Node → Node → Prop}

/-- Avoiding more nodes is harder. -/
theorem Reach.mono {A B : Node → Prop} (hAB : ∀ n, A n → B n) {x y : Node}
    (h : Reach S B x y) : Reach S A x y := by
  induction h with
  | single h => exact .single h
  | cons h hm _ ih => exact .cons h (fun ha => hm (hAB _ ha)) ih

theorem Reach.append {A : Node → Prop} {x m y : Node} (h₁ : Reach S A x m) (hm : ¬ A m)
    (h₂ : Reach S A m y) : Reach S A x y := by
  induction h₁ with
  | single h => exact .cons h hm h₂
  | cons h hm' _ ih => exact .cons h hm' (ih hm h₂)

/-- A path that must pass `m` has a prefix ending at the first `m`. -/
theorem Reach.prefix {A : Node → Prop} {m x y : Node} (h : Reach S A x y)
    (hn : ¬ Reach S (fun n => A n ∨ n = m) x y) : Reach S A x m := by
  induction h with
  | single h => exact absurd (.single h) hn
  | @cons a m' b hs hm' _ ih =>
    by_cases hmm : m' = m
    · subst hmm
      exact .single hs
    · exact .cons hs hm' (ih fun h' => hn (.cons hs (fun h'' => h''.elim hm' hmm) h'))

/-- A path as a list of nodes. -/
theorem Reach.toList {A : Node → Prop} {x y : Node} (h : Reach S A x y) :
    ∃ l : List Node, 2 ≤ l.length ∧ l[0]? = some x ∧ l[l.length - 1]? = some y ∧
      (∀ k a b, l[k]? = some a → l[k + 1]? = some b → S a b) ∧
      ∀ k n, 0 < k → k + 1 < l.length → l[k]? = some n → ¬ A n := by
  induction h with
  | @single a b h =>
    refine ⟨[a, b], by simp, by simp, by simp, ?_, ?_⟩
    · intro k a' b' ha hb
      match k with
      | 0 =>
        simp at ha hb
        subst ha hb
        exact h
      | k + 1 => simp at hb
    · intro k n h0 hk
      simp at hk
      omega
  | @cons a m b hs hm _ ih =>
    obtain ⟨l, hlen, h0, hlast, hstep, havoid⟩ := ih
    refine ⟨a :: l, by simp; omega, by simp, ?_, ?_, ?_⟩
    · simp only [List.length_cons, Nat.add_sub_cancel]
      rw [show l.length = (l.length - 1) + 1 by omega, List.getElem?_cons_succ]
      exact hlast
    · intro k a' b' ha hb
      match k with
      | 0 =>
        simp at ha
        subst ha
        rw [List.getElem?_cons_succ, h0] at hb
        cases hb
        exact hs
      | k + 1 =>
        rw [List.getElem?_cons_succ] at ha hb
        exact hstep k a' b' ha hb
    · intro k n h0' hk hn
      match k with
      | 0 => omega
      | 1 =>
        rw [List.getElem?_cons_succ, h0] at hn
        cases hn
        exact hm
      | k + 2 =>
        rw [List.getElem?_cons_succ] at hn
        simp at hk
        exact havoid (k + 1) n (by omega) (by omega) hn

end Reach

/-! ## First and latest positions -/

theorem exists_min {Q : Nat → Prop} {n : Nat} (h : Q n) :
    ∃ m, Q m ∧ m ≤ n ∧ ∀ k, k < m → ¬ Q k := by
  have aux : ∀ n, (∃ k, k ≤ n ∧ Q k) → ∃ m, Q m ∧ ∀ k, k < m → ¬ Q k := by
    intro n
    induction n with
    | zero =>
      rintro ⟨k, hk, hq⟩
      exact ⟨k, hq, fun j hj => absurd hj (by omega)⟩
    | succ n ih =>
      rintro ⟨k, hk, hq⟩
      by_cases hlow : ∃ j, j ≤ n ∧ Q j
      · exact ih hlow
      · have hk' : k = n + 1 := by
          rcases Nat.lt_or_ge n k with h' | h'
          · omega
          · exact absurd ⟨k, h', hq⟩ hlow
        subst hk'
        exact ⟨n + 1, hq, fun j hj hqj => hlow ⟨j, by omega, hqj⟩⟩
  obtain ⟨m, hm, hmin⟩ := aux n ⟨n, Nat.le_refl _, h⟩
  refine ⟨m, hm, ?_, hmin⟩
  rcases Nat.lt_or_ge n m with h' | h'
  · exact absurd h (hmin n h')
  · exact h'

/-- The first position after `α` and at most `β` that satisfies `Q`. -/
theorem exists_first {Q : Nat → Prop} {α β : Nat} (h : ∃ γ, α < γ ∧ γ ≤ β ∧ Q γ) :
    ∃ γ, α < γ ∧ γ ≤ β ∧ Q γ ∧ ∀ δ, α < δ → δ < γ → ¬ Q δ := by
  obtain ⟨γ, h1, h2, h3⟩ := h
  obtain ⟨m, ⟨hm1, hm2⟩, hmγ, hmin⟩ := exists_min (Q := fun k => α < k ∧ Q k) ⟨h1, h3⟩
  exact ⟨m, hm1, by omega, hm2, fun δ hδ hδm hq => hmin δ hδm ⟨hδ, hq⟩⟩

/-- The latest position before `β` that satisfies `Q`. -/
theorem exists_latest {Q : Nat → Prop} {β : Nat} (h : ∃ α, α < β ∧ Q α) :
    ∃ α, α < β ∧ Q α ∧ ∀ γ, α < γ → γ < β → ¬ Q γ := by
  induction β with
  | zero => obtain ⟨α, hα, _⟩ := h; omega
  | succ β ih =>
    by_cases hq : Q β
    · exact ⟨β, by omega, hq, fun γ h1 h2 => by omega⟩
    · obtain ⟨α, hα, hqα⟩ := h
      have hαβ : α < β := by
        rcases Nat.lt_or_ge α β with h' | h'
        · exact h'
        · exact absurd (show α = β by omega ▸ hqα) hq
      obtain ⟨α', h1, h2, h3⟩ := ih ⟨α, hαβ, hqα⟩
      refine ⟨α', by omega, h2, fun γ hγ1 hγ2 hqγ => ?_⟩
      rcases Nat.lt_or_ge γ β with h' | h'
      · exact h3 γ hγ1 h' hqγ
      · exact hq (show γ = β by omega ▸ hqγ)

/-! ## Runs -/

/-- A run of program `P`: a sequence of nodes that starts at `init` and takes a
CFG step between consecutive positions. -/
structure Run (P : Program) (r : Nat → Option Node) : Prop where
  init : r 0 = some .init
  pre : ∀ {p q : Nat}, q ≤ p → (∃ x, r p = some x) → ∃ y, r q = some y
  step : ∀ {p : Nat} {x y : Node}, r p = some x → r (p + 1) = some y → P.Step x y

section Run

variable {P : Program} {r : Nat → Option Node}

/-- The segment of a run between `α` and `β` is a path, given steps and
avoidance at the positions in between. -/
theorem seg {S : Node → Node → Prop} {A : Node → Prop} {α β : Nat} {a b : Node}
    (hpre : ∀ {p q : Nat}, q ≤ p → (∃ x, r p = some x) → ∃ y, r q = some y)
    (hlt : α < β) (ha : r α = some a) (hb : r β = some b)
    (hS : ∀ γ x y, α ≤ γ → γ < β → r γ = some x → r (γ + 1) = some y → S x y)
    (hA : ∀ γ n, α < γ → γ < β → r γ = some n → ¬ A n) :
    Reach S A a b := by
  obtain ⟨d, rfl⟩ : ∃ d, β = α + d + 1 := ⟨β - α - 1, by omega⟩
  induction d generalizing α a with
  | zero => exact .single (hS α a b (Nat.le_refl _) (by omega) ha (by simpa using hb))
  | succ d ih =>
    obtain ⟨m, hm⟩ := hpre (show α + 1 ≤ α + (d + 1) + 1 by omega) ⟨b, hb⟩
    refine .cons (hS α a m (Nat.le_refl _) (by omega) ha hm)
      (hA (α + 1) m (by omega) (by omega) hm) ?_
    exact ih (α := α + 1) (a := m) (hlt := by omega) (ha := hm)
      (hb := by rw [show α + 1 + d + 1 = α + (d + 1) + 1 by omega]; exact hb)
      (hS := fun γ x y h1 h2 => hS γ x y (by omega) (by omega))
      (hA := fun γ n h1 h2 => hA γ n (by omega) (by omega))

/-- If no path avoids `A`, the segment between `α` and `β` hits `A`. -/
theorem seg_hit {S : Node → Node → Prop} {A : Node → Prop} {α β : Nat} {a b : Node}
    (hpre : ∀ {p q : Nat}, q ≤ p → (∃ x, r p = some x) → ∃ y, r q = some y)
    (hn : ¬ Reach S A a b) (hlt : α < β) (ha : r α = some a) (hb : r β = some b)
    (hS : ∀ γ x y, α ≤ γ → γ < β → r γ = some x → r (γ + 1) = some y → S x y) :
    ∃ γ n, α < γ ∧ γ < β ∧ r γ = some n ∧ A n :=
  Classical.byContradiction fun hno =>
    hn (seg hpre hlt ha hb hS fun γ n h1 h2 hγ hA => hno ⟨γ, n, h1, h2, hγ, hA⟩)

namespace Run

variable (hr : Run P r)
include hr

theorem reach {A : Node → Prop} {α β : Nat} {a b : Node}
    (hlt : α < β) (ha : r α = some a) (hb : r β = some b)
    (hA : ∀ γ n, α < γ → γ < β → r γ = some n → ¬ A n) :
    P.reach A a b :=
  seg hr.pre hlt ha hb (fun _ _ _ _ _ hx hy => hr.step hx hy) hA

theorem hit {A : Node → Prop} {α β : Nat} {a b : Node}
    (hn : ¬ P.reach A a b) (hlt : α < β) (ha : r α = some a) (hb : r β = some b) :
    ∃ γ n, α < γ ∧ γ < β ∧ r γ = some n ∧ A n :=
  seg_hit hr.pre hn hlt ha hb (fun _ _ _ _ _ hx hy => hr.step hx hy)

theorem isSome {p q : Nat} (hqp : q ≤ p) {x : Node} (hx : r p = some x) :
    ∃ y, r q = some y :=
  hr.pre hqp ⟨x, hx⟩

/-- `init` only occurs at position 0. -/
theorem init_only {p : Nat} (h : r p = some .init) : p = 0 := by
  match p with
  | 0 => rfl
  | p + 1 =>
    obtain ⟨x, hx⟩ := hr.isSome (Nat.le_succ p) h
    have := hr.step hx h
    cases this

/-- Dominance in a run: if no path from `init` to `b` avoids `a`, every
`b`-event comes after an `a`-event. -/
theorem dom {a b : Node} {β : Nat} (hab : a = .init ∨ ¬ P.reach (· = a) .init b)
    (hb : b ≠ .init) (hβ : r β = some b) : ∃ α, α < β ∧ r α = some a := by
  have hβ0 : 0 < β := by
    rcases Nat.eq_zero_or_pos β with h | h
    · subst h
      rw [hr.init] at hβ
      cases hβ
      exact absurd rfl hb
    · exact h
  rcases hab with rfl | hn
  · exact ⟨0, hβ0, hr.init⟩
  · obtain ⟨γ, n, _, h2, h3, rfl⟩ := hr.hit hn hβ0 hr.init hβ
    exact ⟨γ, h2, h3⟩

end Run

end Run

/-! ## Unique positions -/

namespace Program

variable {P : Program}

theorem IsFirst.getElem {b : BlockId} {f : NodeId} (h : P.IsFirst b f) :
    (P.code b)[0]? = some f := by
  unfold IsFirst at h
  rw [List.head?_eq_getElem?] at h
  exact h

theorem IsTerm.getElem {b : BlockId} {x : NodeId} (h : P.IsTerm b x) :
    (P.code b)[(P.code b).length - 1]? = some x := by
  unfold IsTerm at h
  rw [List.getLast?_eq_getElem?] at h
  exact h

theorem IsFirst.fn {b : BlockId} {f f' : NodeId} (h : P.IsFirst b f) (h' : P.IsFirst b f') :
    f = f' := by
  unfold IsFirst at h h'
  rw [h] at h'
  exact Option.some.inj h'

theorem IsTerm.fn {b : BlockId} {x x' : NodeId} (h : P.IsTerm b x) (h' : P.IsTerm b x') :
    x = x' := by
  unfold IsTerm at h h'
  rw [h] at h'
  exact Option.some.inj h'

theorem IsFirst.inj (hP : P.WellFormed) {b b' : BlockId} {f : NodeId} (h : P.IsFirst b f)
    (h' : P.IsFirst b' f) : b = b' :=
  (hP.unique _ _ _ _ _ h.getElem h'.getElem).1

theorem IsTerm.inj (hP : P.WellFormed) {b b' : BlockId} {x : NodeId} (h : P.IsTerm b x)
    (h' : P.IsTerm b' x) : b = b' :=
  (hP.unique _ _ _ _ _ h.getElem h'.getElem).1

/-- A step into the first instruction of a block other than the entry is a
jump from the terminator of a predecessor. -/
theorem Step.into_first (hP : P.WellFormed) {J : BlockId} {f : NodeId} {x : Node}
    (hJ : J ≠ P.entry) (hf : P.IsFirst J f) (h : P.Step x (.instr f)) :
    ∃ t p, x = .instr t ∧ P.IsTerm p t ∧ P.IsPred p J := by
  cases h with
  | next hb hx hy =>
    have := (hP.unique _ _ _ _ _ hy hf.getElem).2
    omega
  | jump hb hb' hx hy =>
    have := hy.inj hP hf
    subst this
    exact ⟨_, _, rfl, hx, hb, hb'⟩
  | start hy => exact absurd (hy.inj hP hf).symm hJ

end Program

namespace Run

variable {P : Program} {r : Nat → Option Node} (hr : Run P r)
include hr

/-- An entry into a block other than the entry block comes right after the
terminator of a predecessor. -/
theorem entry (hP : P.WellFormed) {J : BlockId} {f : NodeId} {ε : Nat} (hJ : J ≠ P.entry)
    (hf : P.IsFirst J f) (hε : r ε = some (.instr f)) :
    ∃ e₀ t p, ε = e₀ + 1 ∧ r e₀ = some (.instr t) ∧ P.IsTerm p t ∧ P.IsPred p J := by
  match ε with
  | 0 =>
    rw [hr.init] at hε
    cases hε
  | e₀ + 1 =>
    obtain ⟨x, hx⟩ := hr.isSome (Nat.le_succ e₀) hε
    obtain ⟨t, p, rfl, ht, hp⟩ := (hr.step hx hε).into_first hP hJ hf
    exact ⟨e₀, t, p, rfl, hx, ht, hp⟩

end Run

end Depvec
