# DepVec

DepVec extracts dependency flow graphs from LLVM IR and checks verification
conditions over graph nodes.

## How it works

```
C source ──clang──▶ LLVM IR ──DepVec pass──▶ graph JSON ──▶ Graph Studio (view and annotate)
                                                 │
                                                 └──▶ backend: weakest preconditions → verification conditions → Z3
```

1. Mark the functions to analyze with `DEPVEC_ANALYZE`, from
   `examples/programs/depvec.h`, and compile the C source to textual LLVM IR.
2. The LLVM pass in `src/extractor/` builds a dependency graph for each marked
   function and writes the graphs as `depvec-graph-v2` JSON.
3. Graph Studio (`src/frontend/`) shows each graph next to its IR. Use it to
   edit the layout and the assertions.
4. The backend reads the graph, computes a weakest precondition for each node,
   and uses Z3 to check that each node's precondition implies it.

## Quickstart

`scripts/depvec-studio` builds the pass, exports graphs from an IR file, and
opens Graph Studio on localhost:

```sh
make compile-examples
./scripts/depvec-studio build/examples/ir/message_passing.ll
```

It also accepts an existing graph JSON file. The script needs LLVM's `opt` on
`PATH`, for example inside `nix develop`, or set `LLVM_OPT` to its path.

## Assertion language

Preconditions, postconditions, and edge assertions in the graph JSON are
written in a small formula language. Its ANTLR grammar is
`src/backend/DepVecAssertion.g4`.

- Values: integers, SSA values (`%x`), globals (`@g`), and dereferences
  (`*%p`, `*@g`).
- Arithmetic: `+ - * /` and unary `-`.
- Comparisons: `= != < <= > >=`.
- Points-to facts: `buf |-> 0` for an alloca named `buf`, and `@g |-> 1` for a
  global.
- Logic: `true`, `false`, `!`, `&&`, `||`, and `->`. Implication (`->`) binds
  loosest and associates to the right.
- Types: bit widths are ignored, except that `i1` values are booleans. A
  boolean value is a formula of its own (`%cmp`, `!%cmp`) and compares only
  with `=` and `!=` (`%cmp = false`).
- Members: a `cmpxchg` result `%r` is named only through its members, `%r[0]`
  (the old value) and `%r[1]` (the success flag, a boolean).

For example: `@ready |-> 1 -> @shared_value |-> 42`.

A data edge into a `phi` gets its value from a different source on each path,
so it has no assertion of its own. Instead, `incomingAssertions` holds one
assertion per predecessor block, each with the source that defines that path's
value.

## Project layout

- `src/extractor/`: LLVM pass that analyzes annotated functions and emits
  `depvec-graph-v2` JSON graphs.
- `src/backend/`: standalone C++17 graph reader and verifier. Public headers are
  in `src/backend/include/depvec/`; parsing, weakest-precondition generation,
  and verification-condition checking are implemented here.
- `src/backend/solver/`: SMT-LIB adapter and Z3 engine. The solver abstraction
  is `src/backend/include/depvec/solver.hpp`; integers use mathematical
  integer semantics.
- `src/frontend/`: static graph viewer/editor.
- `test/backend/`: Catch2 tests for parsing, solver behavior, and verification
  conditions.
- `examples/`: sample C programs and graph JSON inputs.

## Further documentation

- `GRAPH_FORMAT.md`: the versioned graph JSON format.
- `llvm-support.md`: which LLVM instructions are supported and how
  pointers and memory orderings are modeled.
- `src/frontend/README.md`: Graph Studio workflows.
- `ideas.md`: design notes and the roadmap.
- `AGENTS.md`: guidelines for contributors and coding agents.

## Build and run

Build commands can be run through the Nix development environment. The Makefile
targets below enter that environment where needed:

- `make build-backend` builds the standalone backend under `build/backend`.
- `make test-backend` builds the backend and runs its tests.
- `make parse-graph GRAPH=examples/graphs/minimal-graph.json` parses a graph.
- `make build-pass` builds the LLVM pass.
- `make test-extractor` runs the extractor's interference tests against a
  brute-force unrolling oracle.
- `make compile-examples`, then `make pass IR=build/examples/ir/example.ll` runs
  the pass on an example IR file.
- `make graphs IR=build/examples/ir/example.ll` exports graph JSON to
  `build/examples/graphs/example.depvec-graph.json` (override with `OUT=`).
- `make example-graphs` compiles every C example and exports its graphs.
- `make clean` removes all build outputs; `clean-extractor`, `clean-backend`,
  and `clean-examples` remove one part each.

`flake.nix` defines the development shell and provides LLVM 22, CMake/Ninja,
ANTLR, Catch2, and Z3. See `Makefile` for the other targets and the variables
they require. A bare `make` runs `compile-ir`, which requires
`SOURCE=path/to/file.c`.
