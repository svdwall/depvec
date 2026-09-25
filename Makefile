.PHONY: compile-ir compile-examples build-pass nix-build pass

EXAMPLE_C_SOURCES := $(wildcard examples/*.c)
EXAMPLE_LL_FILES := $(patsubst %.c,%.ll,$(EXAMPLE_C_SOURCES))

# Compile one C source to optimized textual LLVM IR without loading DepVec.
# Usage: make compile-ir SOURCE=examples/def_use_independent.c
compile-ir:
	@test -n "$(SOURCE)" || (echo "Set SOURCE=path/to/file.c" >&2; exit 1)
	clang -O2 -fno-discard-value-names -S -emit-llvm \
		"$(SOURCE)" -o "$(patsubst %.c,%.ll,$(SOURCE))"

# Compile every C example to its matching LLVM IR file.
compile-examples: $(EXAMPLE_LL_FILES)

examples/%.ll: examples/%.c examples/depvec.h
	clang -O2 -fno-discard-value-names -S -emit-llvm "$<" -o "$@"

build-pass:
	nix build .#depvec-pass --out-link result

nix-build: build-pass

# Run only DepVec on an existing .ll file.
# Usage: make pass IR=examples/def_use_independent.ll
pass: build-pass
	@test -n "$(IR)" || (echo "Set IR=path/to/file.ll" >&2; exit 1)
	opt \
		-load-pass-plugin="$(firstword $(wildcard result/lib/DepVecPass.*))" \
		-passes=depvec -disable-output "$(IR)"
