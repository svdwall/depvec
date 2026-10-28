{
  description = "depvec - *Dep*endency Flow Graph Extractor and *Ve*rification-*C*ondition Checker";

  outputs = { self, nixpkgs, flake-utils }:
  flake-utils.lib.eachDefaultSystem (system:
  with import nixpkgs { inherit system; config.allowUnfree = true; };
  {
    packages.depvec-pass = llvmPackages_22.stdenv.mkDerivation {
      pname = "depvec-pass";
      version = "0.1.0";
      src = lib.cleanSourceWith {
        src = self;
        filter = path: type:
          let
            relativePath = lib.removePrefix "${toString self}/" (toString path);
          in
            toString path == toString self
            || relativePath == "CMakeLists.txt"
            || relativePath == "src"
            || relativePath == "src/extractor"
            || lib.hasPrefix "src/extractor/" relativePath;
      };

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
        antlr4
        antlr4.runtime.cpp
        nlohmann_json
        catch2_3
        z3
        llvmPackages_22.clang
        llvmPackages_22.clang-tools
        llvmPackages_22.libllvm
        pre-commit
        claude-code
        codex
      ];

      shellHook = ''
        export DEPVEC_CXX_COMPILER="${llvmPackages_22.clang}/bin/clang++"
        export CLANGD_FLAGS="--query-driver=${llvmPackages_22.clang}/bin/clang++"
        export DEPVEC_CXX_INCLUDE="${llvmPackages_22.libcxx.dev}/include/c++/v1"
      '';
    };
  });
}
