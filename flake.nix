{
  description = "Inspector - C++ REPL integration";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
    flake-parts.inputs.nixpkgs-lib.follows = "nixpkgs";
    treefmt-nix.url = "github:numtide/treefmt-nix";
    treefmt-nix.inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs =
    inputs@{ flake-parts, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      imports = [
        inputs.treefmt-nix.flakeModule
      ];

      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "aarch64-darwin"
        "x86_64-darwin"
      ];

      perSystem =
        {
          config,
          self',
          inputs',
          pkgs,
          system,
          ...
        }:
        let
          # Use LLVM 20 stdenv for consistency
          llvmPackages = pkgs.llvmPackages_git;
          stdenv = llvmPackages.stdenv;

          # Build LLVM with static libraries
          llvm = llvmPackages.llvm;

          # Package types-pygments
          types-pygments = pkgs.python3.pkgs.buildPythonPackage rec {
            pname = "types_pygments";
            version = "2.19.0.20250809";

            src = pkgs.fetchPypi {
              inherit pname version;
              hash = "sha256-ATZv2T73PHkubuFkmNOr96GE8WJLULd/lQakfthZdMI=";
            };

            format = "setuptools";

            # This is a type stubs package, no runtime dependencies
            propagatedBuildInputs = [ ];

            # No tests in the PyPI package
            doCheck = false;

            pythonImportsCheck = [ "pygments-stubs" ];
          };

          pythonEnv = pkgs.python3.withPackages (
            ps: with ps; [
              prompt-toolkit
              pygments
              setuptools
              libclang
            ]
          );

          inspector = stdenv.mkDerivation rec {
            pname = "inspector";
            version = "0.1.0";

            src = ./.;

            nativeBuildInputs = with pkgs; [
              cmake
              ninja
              pkg-config
              pythonEnv
            ];

            buildInputs = with pkgs; [
              jsoncpp
              llvmPackages.clang-unwrapped.dev
              llvmPackages.clang-unwrapped.lib
              llvm.dev
            ];

            cmakeFlags = [
              "-DCLANG_LIBDIR=${pkgs.lib.getLib llvmPackages.clang-unwrapped}/lib"
            ];
          };
        in
        {
          packages.default = inspector;

          checks =
            let
              packages = pkgs.lib.mapAttrs' (n: pkgs.lib.nameValuePair "package-${n}") self'.packages;
              devShells = pkgs.lib.mapAttrs' (n: pkgs.lib.nameValuePair "devShell-${n}") self'.devShells;
            in
            packages // devShells;

          devShells.default = llvmPackages.stdenv.mkDerivation {
            name = "inspector-dev-shell";
            buildInputs = with pkgs; [
              # Build tools
              cmake
              ninja
              pkg-config

              # C++ dependencies
              jsoncpp
              zlib
              llvmPackages.clang-unwrapped.dev
              llvmPackages.clang-unwrapped.lib
              llvmPackages.clang
              llvmPackages.lldb
              llvm.dev

              # Python environment
              pythonEnv

              # Development tools
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

          treefmt = {
            projectRootFile = "flake.nix";
            programs = {
              nixfmt.enable = true;
              clang-format.enable = true;
              cmake-format.enable = true;
              ruff-check.enable = true;
              ruff-format.enable = true;
              shellcheck.enable = true;
              mypy = {
                enable = true;
                directories = {
                  "python" = {
                    modules = [ "inspector" ];
                    options = [
                      "--strict"
                    ];
                    extraPythonPackages = [
                      pkgs.python3.pkgs.prompt-toolkit
                      pkgs.python3.pkgs.pygments
                      pkgs.python3.pkgs.libclang
                      types-pygments
                    ];
                  };
                };
              };
            };
          };
        };
    };
}
