.PHONY: compile-ir compile-examples build-pass build-backend test-backend build-formal parse-graph nix-build pass graphs example-graphs lint lint-examples lint-all clean clean-extractor clean-backend clean-examples

EXAMPLE_PROGRAMS_DIR := examples/programs
EXAMPLE_BUILD_DIR := build/examples
EXAMPLE_IR_DIR := $(EXAMPLE_BUILD_DIR)/ir
EXAMPLE_GRAPH_DIR := $(EXAMPLE_BUILD_DIR)/graphs
EXAMPLE_C_SOURCES := $(wildcard $(EXAMPLE_PROGRAMS_DIR)/*.c)
EXAMPLE_LL_FILES := $(patsubst $(EXAMPLE_PROGRAMS_DIR)/%.c,$(EXAMPLE_IR_DIR)/%.ll,$(EXAMPLE_C_SOURCES))
EXAMPLE_GRAPH_FILES := $(patsubst $(EXAMPLE_IR_DIR)/%.ll,$(EXAMPLE_GRAPH_DIR)/%.depvec-graph.json,$(EXAMPLE_LL_FILES))
SOURCE_CPP_FILES := $(wildcard src/extractor/*.cpp)
EXTRACTOR_BUILD_DIR := build/extractor
EXTRACTOR_PACKAGE := $(EXTRACTOR_BUILD_DIR)/nix
EXTRACTOR_PLUGIN = $(firstword $(wildcard $(EXTRACTOR_BUILD_DIR)/DepVecPass.*))
BACKEND_BUILD_DIR := build/backend
LLVM_OPT ?= opt

# The opt invocation that loads DepVec, shared by pass and graphs.
DEPVEC_OPT = $(LLVM_OPT) -load-pass-plugin=$(EXTRACTOR_PLUGIN) -passes=depvec -disable-output
GRAPH_OUT = $(or $(OUT),$(EXAMPLE_GRAPH_DIR)/$(notdir $(basename $(IR))).depvec-graph.json)

# Compile one C source to optimized textual LLVM IR without loading DepVec.
# Usage: make compile-ir SOURCE=examples/programs/def_use_independent.c
compile-ir:
	@test -n "$(SOURCE)" || (echo "Set SOURCE=path/to/file.c" >&2; exit 1)
	mkdir -p $(EXAMPLE_IR_DIR)
	clang -O2 -fno-discard-value-names -S -emit-llvm \
		"$(SOURCE)" -o "$(EXAMPLE_IR_DIR)/$(notdir $(basename $(SOURCE))).ll"

# Compile every C example to its matching LLVM IR file.
compile-examples: $(EXAMPLE_LL_FILES)

$(EXAMPLE_IR_DIR)/%.ll: $(EXAMPLE_PROGRAMS_DIR)/%.c $(EXAMPLE_PROGRAMS_DIR)/depvec.h
	mkdir -p $(EXAMPLE_IR_DIR)
	clang -O2 -fno-discard-value-names -S -emit-llvm "$<" -o "$@"

# Configure and build the LLVM pass in the Nix development shell.
build-pass:
	nix develop --command sh -c 'cmake -S . -B $(EXTRACTOR_BUILD_DIR) -G Ninja -DCMAKE_CXX_COMPILER="$$DEPVEC_CXX_COMPILER" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON'
	nix develop --command cmake --build $(EXTRACTOR_BUILD_DIR)

# Configure and build the standalone backend in the Nix development shell.
build-backend:
	nix develop --command sh -c 'cmake -S src/backend -B $(BACKEND_BUILD_DIR) -DCMAKE_CXX_COMPILER="$$DEPVEC_CXX_COMPILER" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON'
	nix develop --command cmake --build $(BACKEND_BUILD_DIR) -j4

# Type-check the Lean formalization of interference in formal/.
build-formal:
	cd formal && nix develop .. --command lake build

# Build and run the standalone backend tests.
test-backend: build-backend
	nix develop --command ctest --test-dir $(BACKEND_BUILD_DIR) --output-on-failure

# Parse a v2 graph JSON file with the backend reader.
# Usage: make parse-graph GRAPH=examples/graphs/minimal-graph.json
parse-graph: build-backend
	@test -n "$(GRAPH)" || (echo "Set GRAPH=path/to/graph.json" >&2; exit 1)
	$(BACKEND_BUILD_DIR)/depvec-graph-parse "$(GRAPH)"

# Build the pass as a Nix package, linked to build/extractor/nix.
nix-build:
	mkdir -p $(EXTRACTOR_BUILD_DIR)
	nix build .#depvec-pass --out-link $(EXTRACTOR_PACKAGE)

# Run only DepVec on an existing .ll file.
# Usage: make pass IR=build/examples/ir/def_use_independent.ll
pass: build-pass
	@test -n "$(IR)" || (echo "Set IR=path/to/file.ll" >&2; exit 1)
	$(DEPVEC_OPT) "$(IR)"

# Export the graphs for annotated functions as JSON for the standalone viewer.
# Writes build/examples/graphs/<name>.depvec-graph.json unless OUT is set.
# Usage: make graphs IR=build/examples/ir/branch_phi.ll [OUT=path/to/graph.json]
graphs: build-pass
	@test -n "$(IR)" || (echo "Set IR=path/to/file.ll" >&2; exit 1)
	mkdir -p "$(dir $(GRAPH_OUT))"
	$(DEPVEC_OPT) -depvec-output="$(GRAPH_OUT)" "$(IR)"

# Export graphs for every C example.
example-graphs: $(EXAMPLE_GRAPH_FILES)

$(EXAMPLE_GRAPH_DIR)/%.depvec-graph.json: $(EXAMPLE_IR_DIR)/%.ll | build-pass
	mkdir -p $(EXAMPLE_GRAPH_DIR)
	$(DEPVEC_OPT) -depvec-output="$@" "$<"

# Run clang-tidy on the pass sources using the CMake compile database.
lint:
	@test -n "$${DEPVEC_CXX_INCLUDE}" || \
		(echo "Run this target inside the Nix devshell to set DEPVEC_CXX_INCLUDE" >&2; exit 1)
	clang-tidy --quiet -p $(EXTRACTOR_BUILD_DIR) \
		--extra-arg-before="-isystem$${DEPVEC_CXX_INCLUDE}" $(SOURCE_CPP_FILES)

# Run clang-tidy on all C examples as C11.
lint-examples:
	clang-tidy --quiet --checks=-bugprone-easily-swappable-parameters \
		--extra-arg-before=-xc --extra-arg-before=-std=c11 \
		--extra-arg-before=-I$(EXAMPLE_PROGRAMS_DIR) $(EXAMPLE_C_SOURCES) --

# Run clang-tidy on both the pass sources and C examples.
lint-all: lint lint-examples

# Remove the extractor build directory, including its Nix result link.
clean-extractor:
	rm -rf $(EXTRACTOR_BUILD_DIR)

# Remove the backend build directory.
clean-backend:
	rm -rf $(BACKEND_BUILD_DIR)

# Remove the compiled example IR and graphs.
clean-examples:
	rm -rf $(EXAMPLE_BUILD_DIR)

# Remove all build outputs.
clean: clean-extractor clean-backend clean-examples
