import Depvec.Graph

/-!
# Unrollings

Sections 2 and 3 of `INTERFERENCE.md`: paths, events, occurrences, source
events, register order, dependency order, and separation.

A path executes each of its blocks completely. Partial executions are covered,
because the events of a prefix and the dependencies between them do not change
when the path is extended.
-/

namespace Depvec

variable (P : Program) (π : List BlockId)

/-- `init · code(b₀) · … · code(b_m)`. Positions are indices into this list,
and `<_π` is `<` on positions. -/
def trace : List Node := .init :: π.flatMap (fun b => (P.code b).map .instr)

/-- `π` is a CFG path starting at the entry block. -/
def IsPath : Prop :=
  π.head? = some P.entry ∧ ∀ k b b', π[k]? = some b → π[k + 1]? = some b' → b' ∈ P.succ b

def nodeAt (p : Nat) : Option Node := (trace P π)[p]?

/-- Position `o` is an occurrence of `e`. -/
def Occurrence (e : Edge) (o : Nat) : Prop := nodeAt P π o = some (.instr e.target)

/-- The last entry into block `J` at or before position `o` is at position
`e₀ + 1`, coming from predecessor `p`, whose terminator is at `e₀`. -/
def LastEntry (J : BlockId) (o e₀ : Nat) (p : BlockId) : Prop :=
  ∃ f x, P.IsFirst J f ∧ e₀ + 1 ≤ o ∧ nodeAt P π (e₀ + 1) = some (.instr f) ∧
    (∀ ε, e₀ + 1 < ε → ε ≤ o → nodeAt P π ε ≠ some (.instr f)) ∧
    nodeAt P π e₀ = some (.instr x) ∧ P.IsTerm p x ∧ P.IsPred p J

/-- `q = σ(o)`: position `q` is the source event of occurrence `o` of `e`. -/
def SourceEvent (e : Edge) (o q : Nat) : Prop :=
  Occurrence P π e o ∧
  match e.source with
  | .plain s =>
      q < o ∧ nodeAt P π q = some s ∧
      ∀ q', q < q' → q' < o → nodeAt P π q' ≠ some s
  | .guarded J sel =>
      ∃ e₀ p, LastEntry P π J o e₀ p ∧ q ≤ e₀ ∧ nodeAt P π q = some (sel p) ∧
        ∀ q', q < q' → q' ≤ e₀ → nodeAt P π q' ≠ some (sel p)

/-- Unrolled dependency `σ(o) →_π o`. -/
def Dep (F : DCG) (π : List BlockId) (q o : Nat) : Prop :=
  ∃ e ∈ F.edges, SourceEvent F.prog π e o q

/-- Register order `q ⇢_π o`: the event at `q` uses or assigns a register
that the later event at `o` assigns. -/
def RegDep (q o : Nat) : Prop :=
  q < o ∧ ∃ u d r, nodeAt P π q = some (.instr u) ∧ nodeAt P π o = some (.instr d) ∧
    (P.uses u r ∨ P.defs u r) ∧ P.defs d r

/-- One step of `≺_π`: `→_π` or `⇢_π`. -/
def Step (F : DCG) (π : List BlockId) (q o : Nat) : Prop :=
  Dep F π q o ∨ RegDep F.prog π q o

/-- `≺_π`, the transitive closure of `→_π ∪ ⇢_π`. -/
inductive DepLt (F : DCG) (π : List BlockId) : Nat → Nat → Prop
  | single {a b} : Step F π a b → DepLt F π a b
  | tail {a b c} : DepLt F π a b → Step F π b c → DepLt F π a c

/-- `⪯_π`. -/
def DepLe (F : DCG) (π : List BlockId) (a b : Nat) : Prop := a = b ∨ DepLt F π a b

/-- `ι ∈ Excl(o) = {o, σ(o)}`. -/
def Excluded (e : Edge) (o ι : Nat) : Prop := ι = o ∨ SourceEvent P π e o ι

/-- Event `ι` is separated from occurrence `o` of `e`. -/
def Separated (F : DCG) (π : List BlockId) (e : Edge) (o ι : Nat) : Prop :=
  (∃ q, SourceEvent F.prog π e o q ∧ DepLt F π ι q) ∨ DepLt F π o ι

end Depvec
