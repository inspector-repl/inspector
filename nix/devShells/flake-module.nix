{
  perSystem =
    { pkgs, ... }:
    let
      llvmPackages = pkgs.llvmPackages_git;
      llvm = llvmPackages.llvm;

      pythonEnv = pkgs.python3.withPackages (
        ps: with ps; [
          prompt-toolkit
          pygments
          setuptools
          libclang
        ]
      );
    in
    {
      devShells = {
        default =
          (pkgs.mkShell.override {
            stdenv = llvmPackages.stdenv;
          })
            {
              buildInputs =
                with pkgs;
                [
                  jsoncpp
                  zlib
                  llvmPackages.clang-unwrapped.dev
                  llvmPackages.clang-unwrapped.lib
                  llvmPackages.clang
                  llvmPackages.lldb
                  llvm.dev
                  pythonEnv
                ]
                ++ pkgs.lib.optionals pkgs.stdenv.isDarwin [
                  libffi
                ];

              nativeBuildInputs = with pkgs; [
                cmake
                ninja
                pkg-config
                (lib.hiPrio pkgs.buildPackages.clang-tools)
              ];

              cmakeFlags = [
                "-DCLANG_LIBDIR=${pkgs.lib.getLib llvmPackages.clang-unwrapped}/lib"
              ];

              shellHook = ''
                echo "Inspector development environment"
                echo "Available tools:"
                echo "  - cmake: Build system"
                echo "  - clang-repl: C++ interpreter (LLVM 19)"
                echo "  - python: With prompt-toolkit and pygments"
                echo "  - nix fmt: Format code"
                echo ""
                echo "To build:"
                echo "  mkdir -p build && cd build"
                echo "  cmake -GNinja .."
                echo "  make"
              '';
            };
      };
    };
}
