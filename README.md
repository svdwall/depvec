# DepVec

DepVec extracts dependency flow graphs from LLVM IR and checks verification
conditions over graph nodes.

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

`GRAPH_FORMAT.md` documents the versioned graph JSON format. See
`src/frontend/README.md` for frontend workflows.

## Build and run

Build commands can be run through the Nix development environment. The Makefile
targets below enter that environment where needed:

- `make build-backend` builds the standalone backend under `build/backend-nix`.
- `make test-backend` builds the backend and runs its tests.
- `make parse-graph GRAPH=examples/graphs/minimal-graph.json` parses a graph.
- `make build-pass` builds the LLVM pass.
- `make compile-examples`, then `make pass IR=build/examples/ir/example.ll` runs
  the pass on an example IR file.
- `make graphs IR=build/examples/ir/example.ll` exports graph JSON.

`flake.nix` defines the development shell and provides LLVM 22, CMake/Ninja,
ANTLR, Catch2, and Z3. Run `make` without a target or inspect `Makefile` for
additional workflows and required variables.
