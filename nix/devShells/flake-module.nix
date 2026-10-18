{
  perSystem =
    { pkgs, ... }:
    let
      llvmPackages = pkgs.llvmPackages_latest;
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
                ++ pkgs.lib.optionals pkgs.stdenv.hostPlatform.isDarwin [
                  libffi
                ];

              nativeBuildInputs = with pkgs; [
                cmake
                ninja
                pkg-config
                (lib.hiPrio pkgs.buildPackages.clang-tools)
              ];

              shellHook = ''
                echo "Inspector development environment"
                echo "To build:"
                echo "  cmake -B build -GNinja && ninja -C build && ctest --test-dir build"
              '';
            };
      };
    };
}
