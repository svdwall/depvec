# LLVM Instruction Support

This file tracks the LLVM instructions we want DepVec to support while we
decide the scope of the LLVM fragment. The list describes desired support; it
does not imply that the parser or solver implements it yet.

## Desired instruction set

### Values and arithmetic

- `add`
- `sub`
- `mul`
- `icmp`

### Memory and atomics

- `alloca`
- `load`
- `store`
- `cmpxchg`
- `extractvalue`

For now, non-atomic `load` and `store` instructions use `Relaxed` in our
instruction model. LLVM `unordered` and `monotonic` orderings also map to
`Relaxed`; the model does not distinguish them from relaxed atomic accesses.

Memory-operation pointers are represented by a separate pointer type and are
restricted to direct pointers produced by `alloca` or to global variables.
Pointer arithmetic, casts, and other pointer origins remain unsupported.

LLVM restricts which orderings are valid for each operation: loads cannot have
release ordering, stores cannot have acquire ordering, and `cmpxchg` has
separate success and failure orderings. The implementation should retain these
orderings explicitly. See the [LLVM Language Reference](https://www.llvm.org/docs/LangRef.html).

### Control flow

- `br`
- `phi`

`ret` is only an exit marker in the graph; it does not need semantic support.

## Scope questions to resolve

Resolve whether loops formed by `br` and `phi` are in the first supported
fragment, or whether the initial fragment is limited to acyclic control flow.
Decide whether weak `cmpxchg` is included; it permits spurious failure.

Choose the supported integer widths and arithmetic flags (`nsw`, `nuw`, and
`exact`), including how poison values are handled.

Other instructions under consideration include `select`, integer casts,
`atomicrmw`, and `fence`. Define which concurrency semantics the solver must
model for atomic orderings and compare-exchange.

Unsupported or undecided instructions should be reported explicitly rather
than silently approximated.
