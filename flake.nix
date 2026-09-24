{
  description = "depvec - *Dep*endency Flow Graph Extractor and *Ve*rification-*C*ondition Checker";

  outputs = { self, nixpkgs, flake-utils }:
  flake-utils.lib.eachDefaultSystem (system:
  with import nixpkgs { inherit system; };
  {
    packages.depvec-pass = llvmPackages_22.stdenv.mkDerivation {
      pname = "depvec-pass";
      version = "0.1.0";
      src = self;

      nativeBuildInputs = [ cmake ninja ];
      buildInputs = [ llvmPackages_22.libllvm ];

      cmakeFlags = [
        "-DLLVM_DIR=${llvmPackages_22.libllvm}/lib/cmake/llvm"
      ];
    };

    packages.default = self.packages.${system}.depvec-pass;

    devShells.default = mkShell {
      packages = [
        cmake
        ninja
        llvmPackages_22.clang
        llvmPackages_22.libllvm
        codex
      ];

      shellHook = ''
        export CLANGD_FLAGS="--query-driver=$(command -v clang++)"
      '';
    };
  });
}
