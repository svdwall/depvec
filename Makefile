.PHONY: compile-ir build-pass pass

# Compile one C source to optimized textual LLVM IR without loading DepVec.
# Usage: make compile-ir SOURCE=examples/def_use_independent.c
compile-ir:
	@test -n "$(SOURCE)" || (echo "Set SOURCE=path/to/file.c" >&2; exit 1)
	clang -O2 -fno-discard-value-names -S -emit-llvm \
		"$(SOURCE)" -o "$(patsubst %.c,%.ll,$(SOURCE))"

build-pass:
	nix build .#depvec-pass --out-link result

# Run only DepVec on an existing .ll file.
# Usage: make pass IR=examples/def_use_independent.ll
pass: build-pass
	@test -n "$(IR)" || (echo "Set IR=path/to/file.ll" >&2; exit 1)
	opt \
		-load-pass-plugin="$(firstword $(wildcard result/lib/DepVecPass.*))" \
		-passes=depvec -disable-output "$(IR)"
