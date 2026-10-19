{
  perSystem =
    { pkgs, ... }:
    let
      llvmPackages = pkgs.llvmPackages_latest;
      llvm = llvmPackages.llvm;
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
                  zlib
                  gtest
                  llvmPackages.clang-unwrapped.dev
                  llvmPackages.clang-unwrapped.lib
                  llvmPackages.clang
                  llvm.dev
                ]
                ++ pkgs.lib.optionals pkgs.stdenv.hostPlatform.isDarwin [
                  libffi
                ];

              nativeBuildInputs = with pkgs; [
                cmake
                ninja
                (lib.hiPrio llvmPackages.clang-tools)
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
