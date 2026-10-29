import Depvec.Static
import Depvec.Trace

/-!
# Soundness

The semantics of `⊲`, the lemmas of `INTERFERENCE.md`, and the soundness
theorem.
-/

namespace Depvec

variable (F : DCG)

/-- `β` is the next `b`-event after position `α`. -/
def NextAfter (π : List BlockId) (b : Node) (α β : Nat) : Prop :=
  α < β ∧ nodeAt F.prog π β = some b ∧
  ∀ γ, α < γ → γ < β → nodeAt F.prog π γ ≠ some b

/-- `o` is the next occurrence of `e` after position `α`. -/
def NextOccAfter (π : List BlockId) (e : Edge) (α o : Nat) : Prop :=
  α < o ∧ Occurrence F.prog π e o ∧
  ∀ γ, α < γ → γ < o → ¬ Occurrence F.prog π e γ

/-- Semantics of `a ⊲ b`: every `a`-event is `≺_π` the next `b`-event after
it. -/
def SemBeforeNext (a b : Node) : Prop :=
  ∀ π, IsPath F.prog π → ∀ α β,
    nodeAt F.prog π α = some a → NextAfter F π b α β → DepLt F π α β

/-- Semantics of `a ⊲ e`: every `a`-event is `⪯_π` the source event of the
next occurrence of `e` after it. -/
def SemBeforeEdge (a : Node) (e : Edge) : Prop :=
  ∀ π, IsPath F.prog π → ∀ α o q,
    nodeAt F.prog π α = some a → NextOccAfter F π e α o →
    SourceEvent F.prog π e o q → DepLe F π α q

/-- The sources of a well-formed edge dominate its target as a set. -/
theorem sources_dominate {e : Edge} (hF : F.WellFormed) (he : e ∈ F.edges) :
    F.prog.SetDom (e.IsSource F.prog) (.instr e.target) := by
  obtain ⟨_, hsrc⟩ := hF.edges e he
  unfold Program.SetDom
  rcases e with ⟨src, t⟩
  cases src with
  | plain s =>
    obtain ⟨_, hs⟩ := hsrc
    rcases hs with rfl | hn
    · exact .inl rfl
    · exact .inr hn
  | guarded J sel =>
    obtain ⟨hJ, _, ⟨f, hf, hdom⟩, hsel⟩ := hsrc
    by_cases hinit : Edge.IsSource F.prog ⟨.guarded J sel, t⟩ .init
    · exact .inl hinit
    refine .inr fun hreach => ?_
    obtain ⟨l, hlen, h0, hlast, hstep, havoid⟩ := hreach.toList
    have hrun : Run F.prog (fun p => l[p]?) :=
      { init := h0
        pre := fun hqp ⟨_, hx⟩ =>
          ⟨_, List.getElem?_eq_getElem (by have := (List.getElem?_eq_some_iff.1 hx).1; omega)⟩
        step := fun hx hy => hstep _ _ _ hx hy }
    have hf' : ∃ ε, ε < l.length - 1 + 1 ∧ l[ε]? = some (.instr f) := by
      rcases hdom with heq | hsd
      · exact ⟨l.length - 1, by omega, heq ▸ hlast⟩
      · obtain ⟨α, hα, hαf⟩ := hrun.dom hsd.2 (by simp) hlast
        exact ⟨α, by omega, hαf⟩
    obtain ⟨ε, hεL, hε⟩ := hf'
    obtain ⟨e₀, x, p, rfl, hx, hxt, hp⟩ := hrun.entry hF.prog hJ hf hε
    obtain ⟨q, hq, hqs⟩ := hrun.dom (hsel p x hp hxt).2 (by simp) hx
    have hq0 : 0 < q := by
      rcases Nat.eq_zero_or_pos q with h | h
      · subst h
        rw [h0] at hqs
        exact absurd ⟨p, hp, (Option.some.inj hqs).symm⟩ hinit
      · exact h
    exact havoid q (sel p) hq0 (by omega) hqs ⟨p, hp, rfl⟩

/-- Every occurrence of a well-formed edge has a source event. -/
theorem sourceEvent_exists {e : Edge} {π : List BlockId} {o : Nat}
    (hF : F.WellFormed) (he : e ∈ F.edges) (hπ : IsPath F.prog π)
    (ho : Occurrence F.prog π e o) :
    ∃ q, SourceEvent F.prog π e o q := by
  have hr := trace_run hF.prog hπ
  obtain ⟨_, hsrc⟩ := hF.edges e he
  rcases e with ⟨src, t⟩
  cases src with
  | plain s =>
    obtain ⟨_, hs⟩ := hsrc
    obtain ⟨α, hα, hαs⟩ := hr.dom hs (by simp) ho
    obtain ⟨q, hq, hqs, hql⟩ :=
      exists_latest (Q := fun q => nodeAt F.prog π q = some s) ⟨α, hα, hαs⟩
    exact ⟨q, ho, hq, hqs, hql⟩
  | guarded J sel =>
    obtain ⟨hJ, _, ⟨f, hf, hdom⟩, hsel⟩ := hsrc
    have hf' : ∃ ε, ε < o + 1 ∧ nodeAt F.prog π ε = some (.instr f) := by
      rcases hdom with heq | hsd
      · exact ⟨o, by omega, heq ▸ ho⟩
      · obtain ⟨α, hα, hαf⟩ := hr.dom hsd.2 (by simp) ho
        exact ⟨α, by omega, hαf⟩
    obtain ⟨ε, hε, hεf, hεl⟩ := exists_latest hf'
    obtain ⟨e₀, x, p, rfl, hx, hxt, hp⟩ := hr.entry hF.prog hJ hf hεf
    obtain ⟨α, hα, hαs⟩ := hr.dom (hsel p x hp hxt).2 (by simp) hx
    obtain ⟨q, hq, hqs, hql⟩ :=
      exists_latest (Q := fun q => nodeAt F.prog π q = some (sel p)) (β := e₀ + 1)
        ⟨α, by omega, hαs⟩
    exact ⟨q, ho, e₀, p,
      ⟨f, x, hf, by omega, hεf, fun ε' h1 h2 => hεl ε' h1 (by omega), hx, hxt, hp⟩,
      by omega, hqs, fun q' h1 h2 => hql q' h1 (by omega)⟩

/-- Two positions that are both the latest `Q` in a range are equal. -/
private theorem latest_unique {Q : Nat → Prop} {q q' β : Nat} (hq : q < β) (hq' : q' < β)
    (h : Q q) (h' : Q q') (hl : ∀ γ, q < γ → γ < β → ¬ Q γ)
    (hl' : ∀ γ, q' < γ → γ < β → ¬ Q γ) : q = q' := by
  rcases Nat.lt_trichotomy q q' with hlt | heq | hgt
  · exact absurd h' (hl q' hlt hq')
  · exact heq
  · exact absurd h (hl' q hgt hq)

/-- The source event of an occurrence is unique. -/
theorem sourceEvent_unique {e : Edge} {π : List BlockId} {o q q' : Nat}
    (hF : F.WellFormed) (h : SourceEvent F.prog π e o q) (h' : SourceEvent F.prog π e o q') :
    q = q' := by
  rcases e with ⟨src, t⟩
  cases src with
  | plain s =>
    obtain ⟨_, hq, hqs, hql⟩ := h
    obtain ⟨_, hq', hqs', hql'⟩ := h'
    exact latest_unique (Q := fun q => nodeAt F.prog π q = some s) hq hq' hqs hqs' hql hql'
  | guarded J sel =>
    obtain ⟨_, e₀, p, ⟨f, x, hf, he1, hef, hel, hx, hxt, _⟩, hq, hqs, hql⟩ := h
    obtain ⟨_, e₀', p', ⟨f', x', hf', he1', hef', hel', hx', hxt', _⟩, hq', hqs', hql'⟩ := h'
    have hff := hf.fn hf'
    subst hff
    have he : e₀ = e₀' := by
      have := latest_unique (Q := fun ε => nodeAt F.prog π ε = some (.instr f)) (β := o + 1)
        (q := e₀ + 1) (q' := e₀' + 1) (by omega) (by omega) hef hef'
        (fun γ h1 h2 => hel γ h1 (by omega)) (fun γ h1 h2 => hel' γ h1 (by omega))
      omega
    subst he
    rw [hx] at hx'
    cases hx'
    have hpp := hxt.inj hF.prog hxt'
    subst hpp
    exact latest_unique (Q := fun q => nodeAt F.prog π q = some (sel p)) (β := e₀ + 1)
      (by omega) (by omega) hqs hqs' (fun γ h1 h2 => hql γ h1 (by omega))
      (fun γ h1 h2 => hql' γ h1 (by omega))

/-- A source event comes before its occurrence. -/
theorem dep_lt {π : List BlockId} {a b : Nat} (h : Dep F π a b) : a < b := by
  obtain ⟨⟨src, t⟩, _, hs⟩ := h
  cases src with
  | plain s => exact hs.2.1
  | guarded J sel =>
    obtain ⟨_, e₀, p, ⟨_, _, _, he1, _⟩, hq, _⟩ := hs
    omega

/-- Lemma 0: `≺_π ⊆ <_π`. -/
theorem depLt_lt {π : List BlockId} {a b : Nat} (h : DepLt F π a b) : a < b := by
  induction h with
  | single h => exact dep_lt F h
  | tail _ h ih => exact Nat.lt_trans ih (dep_lt F h)

theorem DepLt.trans {π : List BlockId} {a b c : Nat} (h₁ : DepLt F π a b)
    (h₂ : DepLt F π b c) : DepLt F π a c := by
  induction h₂ with
  | single h => exact .tail h₁ h
  | tail _ h ih => exact .tail ih h

theorem DepLe.trans_lt {π : List BlockId} {a b c : Nat} (h₁ : DepLe F π a b)
    (h₂ : DepLt F π b c) : DepLt F π a c := by
  rcases h₁ with rfl | h₁
  · exact h₂
  · exact DepLt.trans F h₁ h₂

/-- The target of a `≺₀` chain is an instruction. -/
theorem prec0_instr {a b : Node} (h : Prec0 F a b) : ∃ n, b = .instr n := by
  induction h with
  | edge => exact ⟨_, rfl⟩
  | trans _ _ _ ih => exact ih

/-- A `≺₀` chain is a chain of strict dominance. -/
theorem prec0_dom {a b : Node} (hF : F.WellFormed) (h : Prec0 F a b) :
    a = .init ∨ ¬ F.prog.reach (· = a) .init b := by
  induction h with
  | edge he => exact (hF.edges _ he).2.2
  | @trans a b c hab hbc ih₁ ih₂ =>
    obtain ⟨n, rfl⟩ := prec0_instr F hab
    rcases ih₁ with rfl | h₁
    · exact .inl rfl
    refine .inr fun hc => ?_
    rcases ih₂ with h | h₂
    · cases h
    exact h₁ (hc.prefix fun h' => h₂ (h'.mono fun _ hn => .inr hn))

/-- Lemma 1: if `a ≺₀ b`, then the latest `a` before any `b`-event exists and
is `≺_π` that event. -/
theorem prec0_latest {a b : Node} {π : List BlockId} {ν : Nat}
    (hF : F.WellFormed) (hab : Prec0 F a b) (hπ : IsPath F.prog π)
    (hν : nodeAt F.prog π ν = some b) :
    ∃ α, α < ν ∧ nodeAt F.prog π α = some a ∧
      (∀ γ, α < γ → γ < ν → nodeAt F.prog π γ ≠ some a) ∧
      DepLt F π α ν := by
  have hr := trace_run hF.prog hπ
  induction hab generalizing ν with
  | @edge s t he =>
    obtain ⟨α, hα, hαs⟩ := hr.dom (hF.edges _ he).2.2 (by simp) hν
    obtain ⟨q, hq, hqs, hql⟩ :=
      exists_latest (Q := fun q => nodeAt F.prog π q = some s) ⟨α, hα, hαs⟩
    exact ⟨q, hq, hqs, hql, .single ⟨_, he, hν, hq, hqs, hql⟩⟩
  | @trans a b c hab hbc ih₁ ih₂ =>
    obtain ⟨β, hβ, hβb, hβl, hdep₂⟩ := ih₂ hν
    obtain ⟨α, hα, hαa, hαl, hdep₁⟩ := ih₁ hβb
    refine ⟨α, by omega, hαa, fun γ h1 h2 hγ => ?_, DepLt.trans F hdep₁ hdep₂⟩
    rcases Nat.lt_or_ge γ β with h3 | h3
    · exact hαl γ h1 h3 hγ
    obtain ⟨n, rfl⟩ := prec0_instr F hab
    by_cases ha0 : a = .init
    · subst ha0
      have := hr.init_only hγ
      omega
    have hd₁ := (prec0_dom F hF hab).resolve_left ha0
    rcases prec0_dom F hF hbc with h | hd₂
    · cases h
    -- The first `a`-event, which has no `b` before it.
    obtain ⟨γ₀, hγ₀, hγ₀α, hmin⟩ :=
      exists_min (Q := fun k => nodeAt F.prog π k = some a) hαa
    have hγ₀pos : 0 < γ₀ := by
      rcases Nat.eq_zero_or_pos γ₀ with h | h
      · subst h
        rw [hr.init] at hγ₀
        exact absurd (Option.some.inj hγ₀).symm ha0
      · exact h
    have hinit_a : F.prog.reach (· = .instr n) .init a :=
      hr.reach hγ₀pos hr.init hγ₀ fun δ m h1 h2 hδ hm => by
        subst hm
        obtain ⟨α', hα', hα'a⟩ := hr.dom (.inr hd₁) (by simp) hδ
        exact hmin α' (by omega) hα'a
    have hab_ne : a ≠ .instr n := by
      intro heq
      subst heq
      exact hd₁ (hinit_a.mono fun _ h => h)
    have hγβ : β < γ := by
      rcases Nat.lt_or_ge β γ with h | h
      · exact h
      · have : γ = β := by omega
        subst this
        rw [hβb] at hγ
        cases hγ
        exact absurd rfl hab_ne
    have ha_c : F.prog.reach (· = .instr n) a c :=
      hr.reach h2 hγ hν fun δ m h4 h5 hδ hm => by
        subst hm
        exact hβl δ (by omega) h5 hδ
    exact hd₂ (hinit_a.append hab_ne ha_c)

/-- Lemma 1′: outside the selection region, every event before an occurrence
lies at or before the end of the selecting predecessor. -/
theorem notInRegion_before_entry {J : BlockId} {t : NodeId} {a : Node}
    {π : List BlockId} {α o e₀ : Nat} {p : BlockId}
    (hF : F.WellFormed) (hπ : IsPath F.prog π)
    (ha : ¬ InRegion F J t a) (hα : nodeAt F.prog π α = some a) (hαo : α < o)
    (ho : nodeAt F.prog π o = some (.instr t)) (hentry : LastEntry F.prog π J o e₀ p) :
    α ≤ e₀ := by
  have hr := trace_run hF.prog hπ
  obtain ⟨f, x, hf, he1, hef, hel, _, _, _⟩ := hentry
  refine Classical.byContradiction fun hlt => ha ⟨f, hf, ?_, ?_⟩
  all_goals
    have hS : ∀ γ y z, e₀ + 1 ≤ γ → γ < o → nodeAt F.prog π γ = some y →
        nodeAt F.prog π (γ + 1) = some z → F.prog.StepNoEntry J y z :=
      fun γ y z h1 h2 hy hz => ⟨hr.step hy hz, fun f' hf' heq => by
        have := hf.fn hf'
        subst this heq
        exact hel (γ + 1) (by omega) (by omega) hz⟩
  · rcases Nat.lt_or_ge (e₀ + 1) α with h | h
    · exact .inr (seg hr.pre h hef hα (fun γ y z h1 h2 hy hz => hS γ y z h1 (by omega) hy hz)
        fun _ _ _ _ _ h => h)
    · have : α = e₀ + 1 := by omega
      subst this
      rw [hef] at hα
      exact .inl (Option.some.inj hα).symm
  · exact seg hr.pre hαo hα ho (fun γ y z h1 h2 hy hz => hS γ y z (by omega) h2 hy hz)
      fun _ _ _ _ _ h => h

theorem DepLt.trans_le {π : List BlockId} {a b c : Nat} (h₁ : DepLt F π a b)
    (h₂ : DepLe F π b c) : DepLt F π a c := by
  rcases h₂ with rfl | h₂
  · exact h₁
  · exact DepLt.trans F h₁ h₂

/-- With `a ⊲ a`, every `a`-event is `≺_π` every later `a`-event. -/
theorem chain {a : Node} {π : List BlockId} (h : SemBeforeNext F a a) (hπ : IsPath F.prog π)
    {α α' : Nat} (hlt : α < α') (hα : nodeAt F.prog π α = some a)
    (hα' : nodeAt F.prog π α' = some a) : DepLt F π α α' := by
  have key : ∀ d α, α' - α ≤ d → α < α' → nodeAt F.prog π α = some a → DepLt F π α α' := by
    intro d
    induction d with
    | zero => intro α hd hlt _; omega
    | succ d ih =>
      intro α hd hlt hα
      obtain ⟨β, h1, h2, hβ, hβl⟩ :=
        exists_first (Q := fun k => nodeAt F.prog π k = some a) ⟨α', hlt, Nat.le_refl _, hα'⟩
      have hd₁ := h π hπ α β hα ⟨h1, hβ, hβl⟩
      rcases Nat.eq_or_lt_of_le h2 with rfl | h3
      · exact hd₁
      · exact DepLt.trans F hd₁ (ih β (by omega) h3 hβ)
  exact key _ α (Nat.le_refl _) hlt hα

/-- Recurrence: the first `s` after a point is `⪯_π` the latest `s` before the
next occurrence. -/
theorem recurrence {s : Node} {t : NodeId} {π : List BlockId} (hF : F.WellFormed)
    (hπ : IsPath F.prog π) (hrec : F.prog.reach (· = .instr t) s s → SemBeforeNext F s s)
    {s₁ q : Nat} (hle : s₁ ≤ q) (h₁ : nodeAt F.prog π s₁ = some s)
    (hq : nodeAt F.prog π q = some s)
    (hnt : ∀ γ, s₁ < γ → γ < q → nodeAt F.prog π γ ≠ some (.instr t)) :
    DepLe F π s₁ q := by
  rcases Nat.eq_or_lt_of_le hle with rfl | hlt
  · exact .inl rfl
  · have hr := trace_run hF.prog hπ
    exact .inr (chain F (hrec (hr.reach hlt h₁ hq fun γ n h1 h2 hγ hn => by
      subst hn
      exact hnt γ h1 h2 hγ)) hπ hlt h₁ hq)

theorem sem_r0 {a b : Node} (hF : F.WellFormed) (hab : Prec0 F a b)
    (hrec : F.prog.reach (· = b) a a → SemBeforeNext F a a) : SemBeforeNext F a b := by
  intro π hπ α β hα ⟨hαβ, hβ, hβl⟩
  have hr := trace_run hF.prog hπ
  obtain ⟨α', hα', hα'a, hα'l, hdep⟩ := prec0_latest F hF hab hπ hβ
  have hle : α ≤ α' := by
    rcases Nat.lt_or_ge α' α with h | h
    · exact absurd hα (hα'l α h hαβ)
    · exact h
  rcases Nat.eq_or_lt_of_le hle with rfl | hlt
  · exact hdep
  · have hreach : F.prog.reach (· = b) a a :=
      hr.reach hlt hα hα'a fun γ n h1 h2 hγ hn => by
        subst hn
        exact hβl γ h1 (by omega) hγ
    exact DepLt.trans F (chain F (hrec hreach) hπ hlt hα hα'a) hdep

theorem sem_r1 {a : Node} {e : Edge} (hF : F.WellFormed) (he : e ∈ F.edges)
    (h : SemBeforeEdge F a e) : SemBeforeNext F a (.instr e.target) := by
  intro π hπ α β hα ⟨hαβ, hβ, hβl⟩
  obtain ⟨q, hq⟩ := sourceEvent_exists F hF he hπ hβ
  exact DepLe.trans_lt F (h π hπ α β q hα ⟨hαβ, hβ, hβl⟩ hq) (.single ⟨e, he, hq⟩)

theorem sem_r2 {a b c : Node} (hF : F.WellFormed) (hab : SemBeforeNext F a b)
    (hbc : SemBeforeNext F b c) (hn : ¬ F.prog.reach (· = b) a c) : SemBeforeNext F a c := by
  intro π hπ α γ hα ⟨hαγ, hγ, hγl⟩
  have hr := trace_run hF.prog hπ
  obtain ⟨δ, n, h1, h2, hδ, rfl⟩ := hr.hit hn hαγ hα hγ
  obtain ⟨β, hβ1, hβ2, hβ, hβl⟩ :=
    exists_first (Q := fun k => nodeAt F.prog π k = some n) ⟨δ, h1, Nat.le_refl _, hδ⟩
  exact DepLt.trans F (hab π hπ α β hα ⟨hβ1, hβ, hβl⟩)
    (hbc π hπ β γ hβ ⟨by omega, hγ, fun k h3 h4 => hγl k (by omega) h4⟩)

theorem sem_r3_plain {a s : Node} {t : NodeId} (hF : F.WellFormed)
    (hcov : a = s ∨ ¬ F.prog.reach (fun n => n = s ∨ n = .instr t) a (.instr t))
    (hord : Upcoming F t a s → a ≠ s → SemBeforeNext F a s)
    (hrec : Upcoming F t a s → F.prog.reach (· = .instr t) s s → SemBeforeNext F s s) :
    SemBeforeEdge F a (Edge.mk (.plain s) t) := by
  intro π hπ α o q hα ⟨hαo, ho, hol⟩ ⟨_, hq, hqs, hql⟩
  have hr := trace_run hF.prog hπ
  by_cases has : a = s
  · subst has
    have hle : α ≤ q := by
      rcases Nat.lt_or_ge q α with h | h
      · exact absurd hα (hql α h hαo)
      · exact h
    exact recurrence F hF hπ (hrec (.inl rfl)) hle hα hqs
      fun γ h1 h2 => hol γ h1 (by omega)
  · obtain ⟨γ, n, h1, h2, hγ, hn⟩ := hr.hit (hcov.resolve_left has) hαo hα ho
    rcases hn with rfl | rfl
    · obtain ⟨s₁, hs1, hs2, hs₁, hs₁l⟩ :=
        exists_first (Q := fun k => nodeAt F.prog π k = some n) ⟨γ, h1, Nat.le_refl _, hγ⟩
      have hup : Upcoming F t a n :=
        .inr (hr.reach hs1 hα hs₁ fun δ m h3 h4 hδ hm => by
          subst hm
          exact hol δ h3 (by omega) hδ)
      have hd₁ := hord hup has π hπ α s₁ hα ⟨hs1, hs₁, hs₁l⟩
      have hle : s₁ ≤ q := by
        rcases Nat.lt_or_ge q s₁ with h | h
        · exact absurd hs₁ (hql s₁ h (by omega))
        · exact h
      exact .inr (DepLt.trans_le F hd₁ (recurrence F hF hπ (hrec hup) hle hs₁ hqs
        fun δ h3 h4 => hol δ (by omega) (by omega)))
    · exact absurd hγ (hol γ h1 h2)

theorem sem_r3_guarded {a : Node} {J : BlockId} {sel : BlockId → Node} {t : NodeId}
    (hF : F.WellFormed) (hreg : ¬ InRegion F J t a)
    (hcov : ∀ p, F.prog.IsPred p J → Considered F t a p → CoverGuarded F t a (sel p) p)
    (hord : ∀ p, F.prog.IsPred p J → Considered F t a p →
      Upcoming F t a (sel p) → a ≠ sel p → SemBeforeNext F a (sel p))
    (hrec : ∀ p, F.prog.IsPred p J → Considered F t a p →
      Upcoming F t a (sel p) → F.prog.reach (· = .instr t) (sel p) (sel p) →
      SemBeforeNext F (sel p) (sel p)) :
    SemBeforeEdge F a (Edge.mk (.guarded J sel) t) := by
  intro π hπ α o q hα ⟨hαo, ho, hol⟩ ⟨_, e₀, p, hentry, hq, hqs, hql⟩
  have hr := trace_run hF.prog hπ
  have hle := notInRegion_before_entry F hF hπ hreg hα hαo ho hentry
  obtain ⟨f, x, hf, he1, hef, hel, hx, hxt, hp⟩ := hentry
  have hcons : Considered F t a p := by
    refine ⟨x, hxt, ?_⟩
    rcases Nat.eq_or_lt_of_le hle with rfl | hlt
    · rw [hx] at hα
      exact .inl (Option.some.inj hα).symm
    · exact .inr (hr.reach hlt hα hx fun δ m h1 h2 hδ hm => by
        subst hm
        exact hol δ h1 (by omega) hδ)
  by_cases has : a = sel p
  · subst has
    have hle' : α ≤ q := by
      rcases Nat.lt_or_ge q α with h | h
      · exact absurd hα (hql α h hle)
      · exact h
    exact recurrence F hF hπ (hrec p hp hcons (.inl rfl)) hle' hα hqs
      fun γ h1 h2 => hol γ h1 (by omega)
  · rcases hcov p hp hcons with h | ⟨x', hx't, hax, hn⟩
    · exact absurd h has
    have := hxt.fn hx't
    subst this
    have hlt : α < e₀ := by
      rcases Nat.eq_or_lt_of_le hle with rfl | h
      · rw [hx] at hα
        exact absurd (Option.some.inj hα).symm hax
      · exact h
    obtain ⟨γ, n, h1, h2, hγ, hn⟩ := hr.hit hn hlt hα hx
    rcases hn with rfl | rfl
    · obtain ⟨s₁, hs1, hs2, hs₁, hs₁l⟩ :=
        exists_first (Q := fun k => nodeAt F.prog π k = some (sel p))
          ⟨γ, h1, Nat.le_refl _, hγ⟩
      have hup : Upcoming F t a (sel p) :=
        .inr (hr.reach hs1 hα hs₁ fun δ m h3 h4 hδ hm => by
          subst hm
          exact hol δ h3 (by omega) hδ)
      have hd₁ := hord p hp hcons hup has π hπ α s₁ hα ⟨hs1, hs₁, hs₁l⟩
      have hle' : s₁ ≤ q := by
        rcases Nat.lt_or_ge q s₁ with h | h
        · exact absurd hs₁ (hql s₁ h (by omega))
        · exact h
      exact .inr (DepLt.trans_le F hd₁ (recurrence F hF hπ (hrec p hp hcons hup) hle' hs₁ hqs
        fun δ h3 h4 => hol δ (by omega) (by omega)))
    · exact absurd hγ (hol γ h1 (by omega))

mutual

/-- Lemma 2: every derived `⊲` fact holds semantically. -/
theorem beforeNext_sound (hF : F.WellFormed) :
    {a b : Node} → BeforeNext F a b → SemBeforeNext F a b
  | _, _, .r0 hab hrec => sem_r0 F hF hab fun h => beforeNext_sound hF (hrec h)
  | _, _, .r1 he h => sem_r1 F hF he (beforeEdge_sound hF h)
  | _, _, .r2 h₁ h₂ hn => sem_r2 F hF (beforeNext_sound hF h₁) (beforeNext_sound hF h₂) hn

/-- Lemma 2 for `⊲` toward an edge. -/
theorem beforeEdge_sound (hF : F.WellFormed) :
    {a : Node} → {e : Edge} → BeforeEdge F a e → SemBeforeEdge F a e
  | _, _, .r3Plain _ hcov hord hrec =>
    sem_r3_plain F hF hcov (fun hu hne => beforeNext_sound hF (hord hu hne))
      (fun hu hr => beforeNext_sound hF (hrec hu hr))
  | _, _, .r3Guarded _ hreg hcov hord hrec =>
    sem_r3_guarded F hF hreg hcov
      (fun p hp hc hu hne => beforeNext_sound hF (hord p hp hc hu hne))
      (fun p hp hc hu hr => beforeNext_sound hF (hrec p hp hc hu hr))

end

/-- Soundness: an event of `i` that is not separated from an occurrence of
`e`, and is not one of the edge's own events, makes `i` interfere with `e`. -/
theorem soundness {e : Edge} {i : NodeId} {π : List BlockId} {o ι : Nat}
    (hF : F.WellFormed) (he : e ∈ F.edges) (hπ : IsPath F.prog π)
    (ho : Occurrence F.prog π e o) (hι : nodeAt F.prog π ι = some (.instr i))
    (hexcl : ¬ Excluded F.prog π e o ι) (hsep : ¬ Separated F π e o ι) :
    Interf F e i := by
  intro hss
  obtain ⟨ha, hb, hc, hd⟩ := hss
  apply hsep
  have hr := trace_run hF.prog hπ
  have hne : ι ≠ o := fun h => hexcl (.inl h)
  obtain ⟨q, hq⟩ := sourceEvent_exists F hF he hπ ho
  rcases Nat.lt_or_gt_of_ne hne with hlt | hgt
  · -- `ι` comes before `o`: show `ι ≺_π σ(o)`.
    refine .inl ⟨q, hq, ?_⟩
    obtain ⟨o₁, h1, h2, ho₁, ho₁l⟩ :=
      exists_first (Q := Occurrence F.prog π e) ⟨o, hlt, Nat.le_refl _, ho⟩
    have hreach_it : reachAny F (.instr i) (.instr e.target) :=
      hr.reach h1 hι ho₁ fun _ _ _ _ _ h => h
    have semA := beforeEdge_sound F hF (ha hreach_it)
    have claim : ∀ d o', o' ≤ d → Occurrence F.prog π e o' → o₁ ≤ o' →
        ∀ q', SourceEvent F.prog π e o' q' →
          DepLe F π ι q' ∧ (o₁ < o' → DepLt F π ι q') := by
      intro d
      induction d with
      | zero =>
        intro o' hd _ h3
        omega
      | succ d ih =>
        intro o' hd ho' h3 q' hq'
        rcases Nat.eq_or_lt_of_le h3 with rfl | hlt'
        · exact ⟨semA π hπ ι o₁ q' hι ⟨h1, ho₁, ho₁l⟩ hq', fun h => absurd h (Nat.lt_irrefl _)⟩
        -- The latest occurrence before `o'`.
        obtain ⟨op, hop1, hop, hopl⟩ :=
          exists_latest (Q := Occurrence F.prog π e) ⟨o₁, hlt', ho₁⟩
        have hop₁ : o₁ ≤ op := by
          rcases Nat.lt_or_ge op o₁ with h | h
          · exact absurd ho₁ (hopl o₁ h hlt')
          · exact h
        obtain ⟨qp, hqp⟩ := sourceEvent_exists F hF he hπ hop
        have hιop : DepLt F π ι op :=
          DepLe.trans_lt F (ih op (by omega) hop hop₁ qp hqp).1 (.single ⟨e, he, hqp⟩)
        have hstrict : DepLt F π ι q' := by
          by_cases hi : ∃ γ, op < γ ∧ γ < o' ∧ nodeAt F.prog π γ = some (.instr i)
          · obtain ⟨γ, hγ1, hγ2, hγ⟩ := hi
            obtain ⟨ι', hι1, hι2, hι', hι'l⟩ :=
              exists_first (Q := fun k => nodeAt F.prog π k = some (.instr i))
                ⟨γ, hγ1, Nat.le_refl _, hγ⟩
            have semB := beforeNext_sound F hF (hb (hr.reach hι1 hop hι' fun _ _ _ _ _ h => h))
            have hd₁ := semB π hπ op ι' hop ⟨hι1, hι', hι'l⟩
            have hd₂ := semA π hπ ι' o' q' hι'
              ⟨by omega, ho', fun δ h4 h5 => hopl δ (by omega) h5⟩ hq'
            exact DepLt.trans_le F (DepLt.trans F hιop hd₁) hd₂
          · have hreach_tt : F.prog.reach (· = .instr i) (.instr e.target) (.instr e.target) :=
              hr.reach hop1 hop ho' fun γ n h4 h5 hγ hn => by
                subst hn
                exact hi ⟨γ, h4, h5, hγ⟩
            have semC := beforeEdge_sound F hF (hc hreach_it hreach_tt)
            exact DepLt.trans_le F hιop
              (semC π hπ op o' q' hop ⟨hop1, ho', fun δ h4 h5 => hopl δ h4 h5⟩ hq')
        exact ⟨.inr hstrict, fun _ => hstrict⟩
    obtain ⟨hle, hlt'⟩ := claim o o (Nat.le_refl _) ho h2 q hq
    rcases Nat.eq_or_lt_of_le h2 with heq | h
    · subst heq
      rcases hle with heq | h
      · subst heq
        exact absurd (.inr hq) hexcl
      · exact h
    · exact hlt' h
  · -- `ι` comes after `o`: show `o ≺_π ι`.
    refine .inr ?_
    have claim : ∀ d ι', ι' ≤ d → nodeAt F.prog π ι' = some (.instr i) → o < ι' →
        DepLt F π o ι' := by
      intro d
      induction d with
      | zero =>
        intro ι' hd _ h
        omega
      | succ d ih =>
        intro ι' hbound hι' hoι'
        by_cases hprev : ∃ γ, o < γ ∧ γ < ι' ∧ nodeAt F.prog π γ = some (.instr i)
        · obtain ⟨ιp, hιp1, hιp, hιpl⟩ :=
            exists_latest (Q := fun k => o < k ∧ nodeAt F.prog π k = some (.instr i))
              (β := ι') (by obtain ⟨γ, h1, h2, h3⟩ := hprev; exact ⟨γ, h2, h1, h3⟩)
          have hoιp := ih ιp (by omega) hιp.2 hιp.1
          have hreach_ti : reachAny F (.instr e.target) (.instr i) :=
            hr.reach hιp.1 ho hιp.2 fun _ _ _ _ _ h => h
          by_cases hocc : ∃ γ, ιp < γ ∧ γ < ι' ∧ Occurrence F.prog π e γ
          · obtain ⟨γ, hγ1, hγ2, hγ⟩ := hocc
            obtain ⟨o', ho'1, ho'2, ho', ho'l⟩ :=
              exists_first (Q := Occurrence F.prog π e) ⟨γ, hγ1, Nat.le_refl _, hγ⟩
            obtain ⟨q', hq'⟩ := sourceEvent_exists F hF he hπ ho'
            have semA := beforeEdge_sound F hF
              (ha (hr.reach ho'1 hιp.2 ho' fun _ _ _ _ _ h => h))
            have hd₁ := DepLe.trans_lt F (semA π hπ ιp o' q' hιp.2 ⟨ho'1, ho', ho'l⟩ hq')
              (.single ⟨e, he, hq'⟩)
            have semB := beforeNext_sound F hF (hb hreach_ti)
            have hd₂ := semB π hπ o' ι' ho'
              ⟨by omega, hι', fun δ h4 h5 hδ => hιpl δ (by omega) h5 ⟨by omega, hδ⟩⟩
            exact DepLt.trans F hoιp (DepLt.trans F hd₁ hd₂)
          · have hreach_ii : F.prog.reach (· = .instr e.target) (.instr i) (.instr i) :=
              hr.reach hιp1 hιp.2 hι' fun γ n h4 h5 hγ hn => by
                subst hn
                exact hocc ⟨γ, h4, h5, hγ⟩
            have semD := beforeNext_sound F hF (hd hreach_ti hreach_ii)
            exact DepLt.trans F hoιp (semD π hπ ιp ι' hιp.2
              ⟨hιp1, hι', fun δ h4 h5 hδ => hιpl δ h4 h5 ⟨by omega, hδ⟩⟩)
        · have semB := beforeNext_sound F hF (hb (hr.reach hoι' ho hι' fun _ _ _ _ _ h => h))
          exact semB π hπ o ι' ho
            ⟨hoι', hι', fun δ h4 h5 hδ => hprev ⟨δ, h4, h5, hδ⟩⟩
    exact claim ι ι (Nat.le_refl _) hι hgt

end Depvec
