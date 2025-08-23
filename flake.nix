{
  description = "Inspector - C++ REPL integration";

  nixConfig.extra-substituters = [ "https://cache.thalheim.io" ];
  nixConfig.extra-trusted-public-keys = [
    "cache.thalheim.io-1:R7msbosLEZKrxk/lKxf9BTjOOH7Ax3H0Qj0/6wiHOgc="
  ];

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
        ./nix/packages/flake-module.nix
        ./nix/devShells/flake-module.nix
        ./nix/formatter/flake-module.nix
      ];

      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "aarch64-darwin"
        "x86_64-darwin"
      ];

      perSystem =
        {
          self',
          pkgs,
          ...
        }:
        {
          checks =
            let
              packages = pkgs.lib.mapAttrs' (n: pkgs.lib.nameValuePair "package-${n}") self'.packages;
              devShells = pkgs.lib.mapAttrs' (n: pkgs.lib.nameValuePair "devShell-${n}") self'.devShells;
            in
            packages // devShells;
        };
    };
}
