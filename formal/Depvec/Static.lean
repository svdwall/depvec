import Depvec.Graph

/-!
# Static relations

Section 4 of `INTERFERENCE.md`: the intra-iteration order `≺₀`, selection
regions, the rules R0–R3 for `⊲`, and the Layer 3 table.
-/

namespace Depvec

variable (F : DCG)

/-- `a ≺₀ b`: a chain of plain edges leads from `a` to `b`. -/
inductive Prec0 : Node → Node → Prop
  | edge {s t} : Edge.mk (.plain s) t ∈ F.edges → Prec0 s (.instr t)
  | trans {a b c} : Prec0 a b → Prec0 b c → Prec0 a c

/-- `reach^{∖X}` with nothing to avoid. -/
def reachAny (a b : Node) : Prop := F.prog.reach (fun _ => False) a b

/-- `x ∈ region(e)` for a guarded edge with selection block `J` and target
`t`. -/
def InRegion (J : BlockId) (t : NodeId) (x : Node) : Prop :=
  ∃ f, F.prog.IsFirst J f ∧
    (x = .instr f ∨ F.prog.reachNoEntry J (.instr f) x) ∧
    F.prog.reachNoEntry J x (.instr t)

/-- Source `s` is upcoming from `a` for an edge with target `t`. -/
def Upcoming (t : NodeId) (a s : Node) : Prop :=
  a = s ∨ F.prog.reach (· = .instr t) a s

/-- Predecessor `p` of the selection block can make the next selecting entry
after `a`. -/
def Considered (t : NodeId) (a : Node) (p : BlockId) : Prop :=
  ∃ x, F.prog.IsTerm p x ∧ (a = .instr x ∨ F.prog.reach (· = .instr t) a (.instr x))

/-- Coverage of R3 for a guarded edge and predecessor `p` with source `s`. -/
def CoverGuarded (t : NodeId) (a s : Node) (p : BlockId) : Prop :=
  a = s ∨
  ∃ x, F.prog.IsTerm p x ∧ a ≠ .instr x ∧
    ¬ F.prog.reach (fun n => n = s ∨ n = .instr t) a (.instr x)

mutual

/-- `a ⊲ b`, the least relation closed under R0–R2. A premise `A ∨ B` of the
rules in `INTERFERENCE.md` with `A` static is written as `¬ A → B`. -/
inductive BeforeNext : Node → Node → Prop
  | r0 {a b} :
      Prec0 F a b →
      (F.prog.reach (· = b) a a → BeforeNext a a) →
      BeforeNext a b
  | r1 {a e} :
      e ∈ F.edges → BeforeEdge a e → BeforeNext a (.instr e.target)
  | r2 {a b c} :
      BeforeNext a b → BeforeNext b c → ¬ F.prog.reach (· = b) a c → BeforeNext a c

/-- `a ⊲ e`, derived by R3. -/
inductive BeforeEdge : Node → Edge → Prop
  | r3Plain {a s t} :
      Edge.mk (.plain s) t ∈ F.edges →
      (a = s ∨ ¬ F.prog.reach (fun n => n = s ∨ n = .instr t) a (.instr t)) →
      (Upcoming F t a s → a ≠ s → BeforeNext a s) →
      (Upcoming F t a s → F.prog.reach (· = .instr t) s s → BeforeNext s s) →
      BeforeEdge a (Edge.mk (.plain s) t)
  | r3Guarded {a J sel t} :
      Edge.mk (.guarded J sel) t ∈ F.edges →
      ¬ InRegion F J t a →
      (∀ p, F.prog.IsPred p J → Considered F t a p → CoverGuarded F t a (sel p) p) →
      (∀ p, F.prog.IsPred p J → Considered F t a p →
        Upcoming F t a (sel p) → a ≠ sel p → BeforeNext a (sel p)) →
      (∀ p, F.prog.IsPred p J → Considered F t a p →
        Upcoming F t a (sel p) → F.prog.reach (· = .instr t) (sel p) (sel p) →
        BeforeNext (sel p) (sel p)) →
      BeforeEdge a (Edge.mk (.guarded J sel) t)

end

/-- The Layer 3 table: `i` is separated from `e`. Each row's "unless" is the
premise of an implication. -/
def SeparatedStatic (e : Edge) (i : NodeId) : Prop :=
  let t : Node := .instr e.target
  let x : Node := .instr i
  -- (a)
  (reachAny F x t → BeforeEdge F x e) ∧
  -- (b)
  (reachAny F t x → BeforeNext F t x) ∧
  -- (c)
  (reachAny F x t → F.prog.reach (· = x) t t → BeforeEdge F t e) ∧
  -- (d)
  (reachAny F t x → F.prog.reach (· = t) x x → BeforeNext F x x)

/-- `i ∈ Interf(e)`. -/
def Interf (e : Edge) (i : NodeId) : Prop := ¬ SeparatedStatic F e i

end Depvec
