/-!
# Programs

Section 1 of `INTERFERENCE.md`: blocks, the instruction CFG, reachability that
avoids nodes, and instruction-level dominance.
-/

namespace Depvec

abbrev BlockId := Nat
abbrev NodeId := Nat
abbrev Reg := Nat

/-- A graph node: the virtual `init` marker or an instruction. Exit markers are
instructions that no edge mentions. -/
inductive Node where
  | init
  | instr (n : NodeId)
  deriving DecidableEq

/-- A function's control flow. `code b` lists the instruction nodes of block
`b`, ending in its terminator. `uses n r` and `defs n r` say that instruction
`n` reads or assigns register `r`. -/
structure Program where
  entry : BlockId
  blocks : List BlockId
  code : BlockId → List NodeId
  succ : BlockId → List BlockId
  uses : NodeId → Reg → Prop
  defs : NodeId → Reg → Prop

namespace Program

variable (P : Program)

/-- `n` is the first instruction of block `b`. -/
def IsFirst (b : BlockId) (n : NodeId) : Prop := (P.code b).head? = some n

/-- `n` is the terminator of block `b`. -/
def IsTerm (b : BlockId) (n : NodeId) : Prop := (P.code b).getLast? = some n

/-- `p` is a predecessor block of `J`. -/
def IsPred (p J : BlockId) : Prop := p ∈ P.blocks ∧ J ∈ P.succ p

structure WellFormed : Prop where
  entry_mem : P.entry ∈ P.blocks
  succ_mem : ∀ b ∈ P.blocks, ∀ b' ∈ P.succ b, b' ∈ P.blocks
  code_ne : ∀ b ∈ P.blocks, P.code b ≠ []
  /-- Every instruction node sits at exactly one position of one block. -/
  unique : ∀ (b₁ b₂ : BlockId) (k₁ k₂ : Nat) (n : NodeId),
    (P.code b₁)[k₁]? = some n → (P.code b₂)[k₂]? = some n → b₁ = b₂ ∧ k₁ = k₂

/-- One step of the instruction CFG. A `jump` to the first instruction of `b'`
is an entry into `b'`. -/
inductive Step : Node → Node → Prop
  | next {b : BlockId} {k : Nat} {x y : NodeId} : b ∈ P.blocks → (P.code b)[k]? = some x → (P.code b)[k + 1]? = some y →
      Step (.instr x) (.instr y)
  | jump {b b' x y} : b ∈ P.blocks → b' ∈ P.succ b → P.IsTerm b x → P.IsFirst b' y →
      Step (.instr x) (.instr y)
  | start {y} : P.IsFirst P.entry y → Step .init (.instr y)

/-- A step that is not an entry into block `J`. -/
def StepNoEntry (J : BlockId) (x y : Node) : Prop :=
  P.Step x y ∧ ∀ f, P.IsFirst J f → y ≠ .instr f

end Program

/-- A non-empty path of `S`-steps from `a` to `b` whose nodes strictly in
between do not satisfy `avoid`. -/
inductive Reach (S : Node → Node → Prop) (avoid : Node → Prop) : Node → Node → Prop
  | single {a b} : S a b → Reach S avoid a b
  | cons {a m b} : S a m → ¬ avoid m → Reach S avoid m b → Reach S avoid a b

namespace Program

variable (P : Program)

/-- `reach^{∖X}(a, b)` on the instruction CFG, with `X` given as a predicate. -/
def reach (avoid : Node → Prop) (a b : Node) : Prop := Reach P.Step avoid a b

/-- `reach_{↛J}(a, b)`: a path that takes no entry into block `J`. -/
def reachNoEntry (J : BlockId) (a b : Node) : Prop :=
  Reach (P.StepNoEntry J) (fun _ => False) a b

/-- Instruction-level strict dominance: every path from `init` to `b` passes
`a`. `init` strictly dominates every other node. -/
def SDom (a b : Node) : Prop :=
  a ≠ b ∧ (a = .init ∨ ¬ P.reach (· = a) .init b)

def Dom (a b : Node) : Prop := a = b ∨ P.SDom a b

/-- The set `S` dominates `b`: every path from `init` to `b` passes a node of
`S` before reaching `b`. -/
def SetDom (S : Node → Prop) (b : Node) : Prop :=
  S .init ∨ ¬ P.reach S .init b

end Program

end Depvec
