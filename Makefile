.PHONY: compile-ir compile-examples build-pass build-backend parse-graph nix-build pass graphs lint lint-examples lint-all

EXAMPLE_PROGRAMS_DIR := examples/programs
EXAMPLE_IR_DIR := build/examples/ir
EXAMPLE_C_SOURCES := $(wildcard $(EXAMPLE_PROGRAMS_DIR)/*.c)
EXAMPLE_LL_FILES := $(patsubst $(EXAMPLE_PROGRAMS_DIR)/%.c,$(EXAMPLE_IR_DIR)/%.ll,$(EXAMPLE_C_SOURCES))
SOURCE_CPP_FILES := $(wildcard src/extractor/*.cpp)
EXTRACTOR_BUILD_DIR := build/extractor
EXTRACTOR_PACKAGE := $(EXTRACTOR_BUILD_DIR)/nix
BACKEND_BUILD_DIR := build/backend

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

build-pass:
	mkdir -p $(EXTRACTOR_BUILD_DIR)
	nix build .#depvec-pass --out-link $(EXTRACTOR_PACKAGE)

# Configure and build the standalone backend in the Nix development shell.
build-backend:
	nix develop --command cmake -S src/backend -B $(BACKEND_BUILD_DIR)
	nix develop --command cmake --build $(BACKEND_BUILD_DIR) -j4

# Parse a v2 graph JSON file with the backend reader.
# Usage: make parse-graph GRAPH=examples/graphs/minimal-graph.json
parse-graph: build-backend
	@test -n "$(GRAPH)" || (echo "Set GRAPH=path/to/graph.json" >&2; exit 1)
	$(BACKEND_BUILD_DIR)/depvec-graph-parse "$(GRAPH)"

nix-build: build-pass

# Run only DepVec on an existing .ll file.
# Usage: make pass IR=build/examples/ir/def_use_independent.ll
pass: build-pass
	@test -n "$(IR)" || (echo "Set IR=path/to/file.ll" >&2; exit 1)
	opt \
		-load-pass-plugin=$(EXTRACTOR_PACKAGE)/lib/DepVecPass.* \
		-passes=depvec -disable-output "$(IR)"

# Export the graphs for annotated functions as JSON for the standalone viewer.
# Usage: make graphs IR=build/examples/ir/branch_phi.ll [OUT=depvec-graphs.json]
graphs: build-pass
	@test -n "$(IR)" || (echo "Set IR=path/to/file.ll" >&2; exit 1)
	opt \
		-load-pass-plugin=$(EXTRACTOR_PACKAGE)/lib/DepVecPass.* \
		-depvec-output="$(or $(OUT),depvec-graphs.json)" \
		-passes=depvec -disable-output "$(IR)"

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
