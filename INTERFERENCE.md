# Interference for Dependency Edges: Formal Model

This document defines the interference sets attached to dependency edges in
`depvec-graph-v2` and states the soundness property they should satisfy. It is
a design document; the extractor does not compute interferences yet.

The model, Lemmas 0–2, and the soundness theorem are formalized and proven in
Lean 4 in `formal/`. Run `make build-formal` to check the proofs.

## Goal

A DepVec function has a dependency graph whose edges have one or more sources,
one target `t_e`, and a kind `κ` (data, memory, or control). For each edge, we
want a static set `Interf(e)` of instruction nodes in the same function.

**Guarantee:** if, in some unrolling, an event of an instruction `i` is ordered
neither before nor after an occurrence of `e`, then `i ∈ Interf(e)`. The
occurrence itself and its own source event are not compared with it; other
events of the target and of the sources are.

## 1. Programs

A function is `c = (B, b₀, code, G)`:

- `B` is a set of blocks, and `b₀` is the entry block.
- `code(b)` is the sequence of instruction nodes in block `b`. It ends in a
  terminator `term(b)`: `br b′`, `br x b′ b″`, or `ret`. `first(b)` is the
  first instruction of `b`.
- `succ(b)` is the set of blocks that `b`'s terminator can jump to, and
  `pred(b)` the set of blocks that can jump to `b`.
- `G = (N, E)` is the dependency graph. `N` contains the instruction nodes and
  the `init` and exit markers.

**Dominance** is at instruction level. `a` strictly dominates `b` when `a ≠ b`
and either `a` comes earlier in the same block or `a`'s block strictly
dominates `b`'s block. `init` strictly dominates every instruction.

### Edge kinds

Every edge, of any kind `κ`, is either plain or guarded.

- **Plain edge** `e = (s_e, t_e, κ)`: one source `s_e`, which must strictly
  dominate `t_e`.
- **Guarded edge** `e = (J_e, sel_e, t_e, κ)`: a hyperedge whose source is
  selected by the block from which the path last entered the **selection
  block** `J_e`. `sel_e` maps each `p ∈ pred(J_e)` to a source node. The
  sources of `e` are `S_e = { sel_e(p) | p ∈ pred(J_e) }`, and the same source
  may be selected for several blocks. A guarded edge must satisfy:
  - **(G1)** `J_e ≠ b₀`, and `J_e` dominates `t_e`'s block. So every
    `t_e`-event is preceded by an entry into `J_e`.
  - **(G2)** `sel_e(p)` is defined for every `p ∈ pred(J_e)`, including
    back-edge predecessors if `J_e` is a loop header.
  - **(G3)** `sel_e(p)` strictly dominates `term(p)`. So the selected source
    has always occurred when the path leaves `p`.

For plain edges, write `S_e = {s_e}`. An edge whose source does not dominate
its target must be guarded.

**Lemma (sources dominate).** For every well-formed edge, `S_e` dominates
`t_e` as a set: every path from `init` to `t_e` passes some source before it
reaches `t_e`. For a plain edge, this is strict dominance. For a guarded edge,
it follows from (G1)–(G3): the path enters `J_e` from some `p`, and `sel_e(p)`
occurs before `p` ends. The converse does not hold: set dominance does not say
which source event `σ(o)` is. This lemma is what guarantees that every
occurrence has a source event. Every edge the extractor emits for the example
programs satisfies it.

**Examples:**

- A data edge into a `phi` `t` is guarded, with `J_e = block(t)` and
  `sel_e(p)` the definition of the incoming value from `p`.
- Take two branches that each perform an acquire load, followed by a memory
  access `t` after their join block `J`. This can be one guarded memory edge
  `e = (J, {p₁ ↦ acq₁, p₂ ↦ acq₂}, t)`.
- A loop-carried control edge would be guarded, with `J_e` the loop header
  (see Section 7).

### What the extractor emits

- Data edges into non-phi targets are plain. There is one per target and SSA
  value, so `z = add x, y` has the two edges `x → z` and `y → z`. Repeated uses
  of one value share an edge.
- Each `phi` has exactly one data edge, which is guarded as above.
- Memory and control edges are plain.
- Constant operands have no source node, not even `init`. An instruction whose
  operands are all constants has no data edge. Function arguments are
  represented by `init`.
- Every incoming value of a `phi` must be an instruction or a function
  argument, so that (G2) holds. A `phi` with a constant incoming value is not
  supported for now.

**Instruction CFG.** The reachability relations below run on instructions:
each instruction steps to the next one in its block, and `term(b)` steps to
`first(b′)` for every `b′ ∈ succ(b)`. That last step is an **entry** into
`b′`. `init` is a virtual predecessor of `first(b₀)` and has no predecessors
itself.

## 2. Unrollings

- **Paths:** `π = b₀ … b_m` with `b_{j+1} ∈ succ(b_j)`. These are all CFG path
  prefixes: branch choices are free, and partial executions count.
- **Events:** the positions of `init · code(b₀) · … · code(b_m)`, each labelled
  with a node `ν(p)`. The `init` event is position 0. `<_π` is position order,
  and `≤_π` its reflexive closure.
- **Occurrences:** every event of `t_e` is an occurrence `o` of `e`.
- **Active source `act(o)`:** every occurrence has exactly one.
  - For a plain edge, it is `s_e`.
  - For a guarded edge, take the last entry into `J_e` before `o`. It exists
    by (G1). If that entry comes from `p`, then `act(o) = sel_e(p)`.
- **Source event `σ(o)`:**
  - For a plain edge, it is the latest `s_e` before `o`. It exists because
    `s_e` dominates `t_e`.
  - For a guarded edge, it is the latest `act(o)` up to that last entry, that
    is, at or before the end of `p`. It exists by (G3). Phis of one block read
    their operands in parallel, so a phi that is evaluated earlier in the same
    block does not count as the source. For example, `a = phi(…, b)` and
    `b = phi(…, a)` swap values.
- **Unrolled dependency:** `σ(o) →_π o` for every edge and occurrence. `≺_π`
  is the transitive closure of `→_π`, so it is dependency order, not position
  order. `⪯_π` is its reflexive closure.

## 3. Separation and the Theorem

The **excluded events** of an occurrence `o` are `Excl(o) = {o, σ(o)}`. These
are the edge's own events. Neither direction of separation can hold for them.

An event `ι ∉ Excl(o)` of `i` is **separated** from `o` when:

- **Before:** `ι ≺_π σ(o)`, or
- **After:** `o ≺_π ι`.

**Theorem (soundness).** For all `c`, `e`, and instruction nodes `i`: if some
path `π` has an occurrence `o` of `e` and an event `ι ∉ Excl(o)` of `i` that
are not separated, then `i ∈ Interf(e)`.

Sources and the target of `e` are candidates like any other instruction. Only
their events in `Excl(o)` are skipped. For example, the next iteration's event
of a source can still be unordered with this iteration's occurrence.

**Lemma 0.** `≺_π ⊆ <_π`. So an event `ι` strictly between `σ(o)` and `o` is
never separated: whatever runs on a path between the source and the target
trivially interferes.

## 4. Static Relations

### Layer 1: intra-iteration order `≺₀`

`a ≺₀ b` means a chain of plain edges leads from `a` to `b`. Guarded edges are
not used here. Every plain source strictly dominates its target, so a plain
edge never crosses a back edge to reach its source event.

**Lemma 1.** If `a ≺₀ b`, then for every event `ν` of `b`, the latest `a`
before `ν` exists and is `≺_π ν`.

*Proof.* Use induction on the chain. For one edge, the latest `a` is `σ(ν)`.
For a chain `a → x → … → b`, suppose an `a`-event lay between the latest `x`
before `ν` and `ν`. Then some path would run from that `a` to `b` without
passing `x`. Prefixing it with a path from the entry to `a` that avoids `x`
gives a path from the entry to `b` that avoids `x`. Such a prefix exists
because `a` strictly dominates `x`. That path contradicts `x` dominating `b`.

### Reachability

- **`reach^{∖X}(a, b)`:** some non-empty path in the instruction CFG runs from
  `a` to `b` with no node of `X` strictly in between. Back edges are allowed.
  So `reach(a, a)` means that `a` lies on a cycle. No path reaches `init`.
  Repeated endpoints inside the path do not matter:
  `reach^{∖X}(a, b) ⇔ reach^{∖X ∪ {a, b}}(a, b)`.
- **`reach_{↛J}(a, b)`:** some non-empty path from `a` to `b` takes no entry
  into block `J`.
- **Upcoming source:** a source `s` is upcoming from `a` when `a = s` or
  `reach^{∖t_e}(a, s)`. Paths that visit the target on the way are ignored,
  because those source events belong to a later occurrence.
- **Selection region** of a guarded edge `e`, with `J = J_e` and `t = t_e`:

  ```
  region(e) = { x | (x = first(J) ∨ reach_{↛J}(first(J), x)) ∧ reach_{↛J}(x, t) }
  ```

  These are the nodes that can run after the selecting entry into `J` and
  before an occurrence that uses that entry. For a `phi` target, `region(e)`
  is the set of phis of `J` that come before `t`. `t` itself belongs to
  `region(e)` exactly when it can repeat without re-entering `J`.

**Lemma 1′.** Let `e` be guarded and `a ∉ region(e)`. Then every `a`-event
before an occurrence `o` lies at or before the end of the block that made the
selecting entry into `J_e`.

*Proof.* An `a`-event after the selecting entry and before `o` would give a
path from `first(J_e)` through `a` to `t_e` with no entry into `J_e`. That
would put `a` in `region(e)`.

### Layer 2: "before the next", `⊲`

**Semantics:**

- **`a ⊲ b`:** every `a`-event is `≺_π` the next `b`-event after it.
- **`a ⊲ e`:** every `a`-event `α` satisfies `α ⪯_π σ(o)` for the next
  occurrence `o` of `e` after `α`.

The relation is reflexive only when `a` is a source of `e`. If `a ∉ S_e`,
`α ⪯_π σ(o)` is the same as `α ≺_π σ(o)`. In both cases, `a ⊲ e` implies
`α ≺_π o`.

`⊲` is the least relation closed under these rules:

- **(R0)** If `a ≺₀ b`, and either `¬reach^{∖b}(a, a)` or `a ⊲ a`, then
  `a ⊲ b`.
- **(R1)** If `a ⊲ e`, then `a ⊲ t_e`. That is, "`i` before `e` implies `i`
  before `t`."
- **(R2)** If `a ⊲ b`, `b ⊲ c`, and `¬reach^{∖b}(a, c)`, then `a ⊲ c`. This is
  transitivity: `c` must not occur between `a` and the next `b`.
- **(R3)** `a ⊲ e`, with `t = t_e`, when the conditions below hold for every
  source that can be active at the next occurrence.
  - **Plain `e`, with `s = s_e`:**
    - **Coverage:** `a = s` or `¬reach^{∖{s,t}}(a, t)`.
    - **Ordered source and recurrence** for `s`, as below.
  - **Guarded `e`, with `J = J_e`:** `a ∉ region(e)`. Consider each
    `p ∈ pred(J)` with `a = term(p)` or `reach^{∖t}(a, term(p))`. For each
    such `p`, with `s = sel_e(p)`:
    - **Coverage:** `a = s`, or `a ≠ term(p) ∧ ¬reach^{∖{s,t}}(a, term(p))`.
      The path has to pass `s` before it leaves `p`. An `s` after the
      selecting entry does not count.
    - **Ordered source and recurrence** for `s`, as below.
  - **Ordered source:** if `s` is upcoming from `a`, then `a = s` or `a ⊲ s`.
  - **Recurrence:** if `s` is upcoming from `a`, then `¬reach^{∖t}(s, s)` or
    `s ⊲ s`. This chains the first `s` after `a` to the latest one.

Coverage counts the start node `a` as passing `a`, and no path passes `init`.

All premises about `⊲` are positive, and the reachability premises are static.
So the least relation exists and is a least fixpoint.

**Lemma 2.** Every derived `⊲` fact holds semantically, by induction on
derivations.

For R3, take an `a`-event `α` and the next occurrence `o`, with `s = act(o)`.
No `t`-event lies strictly between `α` and `o`. Let `end` be the end of the
selecting predecessor `p` for a guarded edge, and `o` itself for a plain edge.

- **`p` was considered.** For a guarded edge, `α` lies at or before the end of
  `p`, by Lemma 1′. The path from `α` to that `term(p)` passes no `t`, so `p`
  is one of the blocks considered.
- **If `a ≠ s`:** coverage puts an `s`-event in the interval `(α, end]`, and
  so in `(α, σ(o)]`. Let `s₁` be the first one. Then `s` is upcoming, and the
  ordered-source condition gives `α ≺_π s₁`. Recurrence chains
  `s₁ ⪯_π σ(o)`. In the `¬reach^{∖t}(s, s)` case, `s₁ = σ(o)`.
- **If `a = s`:** `α ≤_π σ(o)`, because `α` lies at or before `end`.
  Recurrence gives `α ⪯_π σ(o)`.

**Loops and `phi` edges come out of these rules.** For a loop header
`h = phi(p from the preheader, c from the latch)`, the data edge `e_h` is
guarded, with `J = block(h)`. `sel(preheader) = p` and `sel(latch) = c`.

- **`h ⊲ h`** holds iff `h ⊲ c`. From `h`, only the latch leads back to `h`,
  so R3 considers only `c`.
- **`x ⊲ h`** for an `x` before the loop holds via `p`. From `x`, only the
  preheader leads to `h`.
- **`x` before a use after the loop** additionally needs `h ⊲ h`:
  - It fails for `r = load(data)`, which is correct: `x` is not ordered before
    the use.
  - It holds for `r = r + load`.

**Acquire loads in both branches.** Take `e = (J, {p₁ ↦ acq₁, p₂ ↦ acq₂}, t)`
for a memory access `t` after the join block `J`. Then `region(e)` contains
everything from `first(J)` up to `t`. Those instructions run after the
selected acquire and before `t`, so they can never be `⊲ e`. An `x` before
the branch satisfies `x ⊲ e` when `x ⊲ acq₁` and `x ⊲ acq₂`, and both acquires
pass recurrence.

### Layer 3: interference

An instruction `i` is **separated** from `e` when all of these hold:

| | Unless | Requirement |
|---|---|---|
| (a) | `¬reach(i, t)` | `i ⊲ e` |
| (b) | `¬reach(t, i)` | `t ⊲ i` |
| (c) | `¬reach(i, t)` or `¬reach^{∖i}(t, t)` | `t ⊲ e` |
| (d) | `¬reach(t, i)` or `¬reach^{∖t}(i, i)` | `i ⊲ i` |

**Rule:** `i ∈ Interf(e)` if and only if `i` is not separated from `e`.

Consequences:

- The target is listed only if its other occurrences can be unordered with an
  occurrence, for example in a loop where `t ⋪ e`. A target outside every loop
  is never listed. For a guarded edge, it is also listed when it can repeat
  without re-entering `J_e`. Then two occurrences share one source event.
- A source is listed only if one of its events other than the source event can
  be unordered with the occurrence.
- "Between" needs no separate rule, because coverage fails for it. For a
  guarded edge, every `i ∈ region(e)` is "between".
- "InBetween" for an `i` in a loop is simply (a) and (b) together.

**Proof sketch.** Take an event `ι ∉ Excl(o)` and an occurrence `o` that comes
after it. Induct on the alternating sequence of `i`-events and occurrences
between them:

- (a) gives `ι ⪯_π σ(o₁)` for the next occurrence `o₁`, and so `ι ≺_π o₁`.
- (c) chains occurrences that have no `i`-event between them.
- (b), then (a), carry the order across an intermediate `i`-event.

The result is `ι ⪯_π σ(o)`. The relation is strict for one of three reasons:
`i ∉ S_e`, or the chain passes through an earlier occurrence, or `ι ≠ σ(o)`
because `ι ∉ Excl(o)`. For `i = t`, "the next occurrence after `ι`" excludes
`ι` itself.

The case where `o` comes before `ι` is symmetric, using (b) and (d).

## 5. Algorithm

1. **Instruction CFG**, as in Section 1, with entries into blocks marked.
2. **Reachability oracle** for `reach^{∖X}(a, b)` with `|X| ≤ 2`, and for
   `reach_{↛J}(a, b)`. Use a search that never expands nodes in `X`, or never
   takes an entry into `J`, memoized per `X` or `J`.
3. **Static facts:** `≺₀`, the closure of plain edges over a DAG; upcoming
   sources; `region(e)` for every guarded edge; and coverage for every
   `(a, e)`.
4. **Fixpoint** of R0–R3 over `a ⊲ b` and `a ⊲ e`, with a semi-naive
   worklist. There are at most `|N|² + |N|·|E|` facts.
5. **`Interf(e)`** from the Layer 3 table, for every instruction node.

**Validation.** A brute-force oracle enumerates all path prefixes up to a bound
on loop iterations. It builds `≺_π` and computes the unseparated pairs exactly
as Section 3 defines them. The computed `Interf(e)` must contain every such
`i`. The difference measures precision. Inputs are the example programs,
targeted cases, and randomly generated small CFGs with well-formed edges.
Targeted cases include:

- nested loops and swapped phis;
- phis with an argument as an incoming value, and a value that reaches a `phi`
  from several blocks;
- `r = load` versus `r = r + load`;
- guarded edges whose selection block is not the target's block, including a
  target in a loop after the selection block.

## 6. Decisions So Far

- Every edge is plain (one dominating source) or guarded (one source per
  predecessor of a selection block that dominates the target). Every
  occurrence has exactly one active source.
- The extractor emits only plain edges, except for one guarded data edge per
  `phi`. Data edges into other targets are split per SSA value.
- Candidates are the instruction nodes of `e`'s own function, including `e`'s
  sources and target. Init and exit markers are excluded.
- An occurrence is compared with every event except the occurrence itself and
  its own source event.
- Unrollings are all CFG path prefixes. Infeasible paths are included, so the
  analysis is sound but may over-report.
- For a plain edge, the source event is the latest event of the source before
  the target event. For a guarded edge, it is the latest event of the selected
  source up to the last entry into the selection block.
- Constant operands have no source. `phi` instructions with constant incoming
  values are not supported for now, so every occurrence has an active source.
- Dominance is strict and at instruction level.
- `Interf(e)` is a pure ordering notion. A consumer narrows it to instructions
  that write memory the edge's assertion reads.

## 7. Open Questions

- **`phi` instructions with constant incoming values:** these are excluded for
  now. Supporting them needs a definition for an occurrence with no active
  source.
- **Format for guarded edges:** for a `phi`, `J_e` and `sel_e` follow from
  `operation.incoming`. Guarded memory or control edges need the selection
  block and the map from predecessor to source in the edge object.
- **Precision on loops without a header `phi`:** for example, the
  `message_passing` reader. No next-iteration ordering can be derived there, so
  all of the loop's instructions interfere with its edges. A loop-carried
  control edge could fix this. It would be guarded, with `J_e` the loop header
  and `sel_e(latch)` the latch branch. (G2) also requires a source for the
  preheader, and it is open which node that should be.
- **Other threads:** instructions from other analyzed functions would always
  interfere. Including them requires function-qualified node references in the
  format. Alternatively, the consumer can add them, without a format change.
- **Unconditional branches:** they have no edges, so they appear as
  interferences on most edges. Under the consumer-side filter above, they drop
  out because they write no memory.
