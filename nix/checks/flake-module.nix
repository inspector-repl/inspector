{
  perSystem =
    { pkgs, self', ... }:
    let
      inherit (pkgs) lib;
      inspector = self'.packages.default;
      llvmPackages = pkgs.llvmPackages_latest;
      e2e = ../../test/e2e.py;
      # One check per build-system integration, all running the same REPL test.
      mkExample =
        name: nativeBuildInputs: build:
        llvmPackages.stdenv.mkDerivation {
          name = "inspector-example-${name}";
          src = ../../examples;
          nativeBuildInputs = [
            inspector
            pkgs.python3
          ]
          ++ nativeBuildInputs;
          dontConfigure = true;
          buildPhase = ''
            runHook preBuild
            export INSPECTOR_CXX=$CXX
            ${build}
            python3 ${e2e} "$PWD/example-bin"
            runHook postBuild
          '';
          installPhase = "touch $out";
        };
    in
    {
      checks = {
        example-make = mkExample "make" [ ] ''
          cd make
          make CXX=inspector-c++
          mv example example-bin
        '';
        example-cmake = mkExample "cmake" [ pkgs.cmake ] ''
          cd cmake
          cmake -B build
          cmake --build build
          mv build/example example-bin
        '';
        example-meson = mkExample "meson" [ pkgs.meson pkgs.ninja ] ''
          cd meson
          CXX=inspector-c++ meson setup build
          ninja -C build
          mv build/example example-bin
        '';
        package-llvm22 = inspector.override {
          llvmPackages = pkgs.llvmPackages_22;
          llvm = pkgs.llvmPackages_22.llvm;
          stdenv = pkgs.llvmPackages_22.stdenv;
        };
      };
    };
}
