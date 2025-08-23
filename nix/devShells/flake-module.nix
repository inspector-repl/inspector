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
          hatchling
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
                echo "To build:"
                echo "  mkdir -p build && cd build"
                echo "  cmake -GNinja .."
                echo "  make"
              '';
            };
      };
    };
}
