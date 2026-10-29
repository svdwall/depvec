import Depvec.Program

/-!
# Dependency graphs

Edge kinds of Section 1 of `INTERFERENCE.md`. The edge kind `κ` (data, memory,
control) does not affect interference and is not modelled.
-/

namespace Depvec

/-- How an edge's source is determined. -/
inductive Source where
  /-- A single source that strictly dominates the target. -/
  | plain (s : Node)
  /-- A source selected by the predecessor block of the last entry into the
  selection block `J`. -/
  | guarded (J : BlockId) (sel : BlockId → Node)

structure Edge where
  source : Source
  target : NodeId

/-- A dependency and control flow graph: a function's control flow and its
dependency edges. -/
structure DCG where
  prog : Program
  edges : List Edge

namespace Edge

variable (P : Program)

/-- `s ∈ S_e`. -/
def IsSource (e : Edge) (s : Node) : Prop :=
  match e.source with
  | .plain s' => s = s'
  | .guarded J sel => ∃ p, P.IsPred p J ∧ sel p = s

/-- Well-formedness of an edge. For a guarded edge, these are (G1)–(G3). -/
def WellFormed (e : Edge) : Prop :=
  (∃ (b : BlockId) (k : Nat), b ∈ P.blocks ∧ (P.code b)[k]? = some e.target) ∧
  match e.source with
  | .plain s => P.SDom s (.instr e.target)
  | .guarded J sel =>
      J ≠ P.entry ∧ J ∈ P.blocks ∧
      (∃ f, P.IsFirst J f ∧ P.Dom (.instr f) (.instr e.target)) ∧
      ∀ p x, P.IsPred p J → P.IsTerm p x → P.SDom (sel p) (.instr x)

end Edge

structure DCG.WellFormed (F : DCG) : Prop where
  prog : F.prog.WellFormed
  edges : ∀ e ∈ F.edges, Edge.WellFormed F.prog e

end Depvec
