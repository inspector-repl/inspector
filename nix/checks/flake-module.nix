{
  perSystem =
    { pkgs, self', ... }:
    let
      inherit (pkgs) lib;
      inspector = self'.packages.default;
      llvmPackages = pkgs.llvmPackages_latest;
      # One check per build-system integration. Builds against the freshly
      # installed (and wrapped) package, then runs inspector-e2e from the
      # build tree.
      mkExample =
        name: nativeBuildInputs: build:
        inspector.overrideAttrs (old: {
          pname = "inspector-example-${name}";
          nativeBuildInputs = old.nativeBuildInputs ++ nativeBuildInputs;
          # Keep doCheck so the tests are built (it sets BUILD_TESTING); the
          # package check already runs them, so only note where the binary is.
          checkPhase = "e2e=$PWD/test/inspector-e2e";
          postFixup = old.postFixup + ''
            cp -r ${../../examples} examples
            chmod -R u+w examples
            (
              export PATH=$out/bin:$PATH INSPECTOR_CXX=$CXX
              cd examples
              ${build}
            )
            "$e2e" "$PWD/examples/${name}/example-bin"
          '';
        });
    in
    {
      checks = {
        example-make = mkExample "make" [ pkgs.gnumake ] ''
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
        # Same clang as clang-tidy so compile_commands carry flags it understands.
        clang-tidy = inspector.overrideAttrs (old: {
          pname = "inspector-clang-tidy";
          nativeBuildInputs = old.nativeBuildInputs ++ [ llvmPackages.clang-tools ];
          cmakeFlags = (old.cmakeFlags or [ ]) ++ [
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
            "-DBUILD_TESTING=ON"
          ];
          # run-clang-tidy has a /usr/bin/env shebang, unavailable in the sandbox.
          # Vendored code keeps its upstream style.
          buildPhase = "python3 $(command -v run-clang-tidy) -quiet -p . '^((?!third_party).)*$'";
          installPhase = "touch $out";
          doCheck = false;
          dontFixup = true;
        });
        package-llvm22 = inspector.override {
          llvmPackages = pkgs.llvmPackages_22;
          llvm = pkgs.llvmPackages_22.llvm;
          stdenv = pkgs.llvmPackages_22.stdenv;
        };
      };
    };
}
