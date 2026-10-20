{
  description = "Inspector - C++ REPL integration";

  nixConfig.extra-substituters = [ "https://cache.thalheim.io" ];
  nixConfig.extra-trusted-public-keys = [
    "cache.thalheim.io-1:R7msbosLEZKrxk/lKxf9BTjOOH7Ax3H0Qj0/6wiHOgc="
  ];

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      inherit (nixpkgs) lib;
      forAllSystems = lib.genAttrs [
        "x86_64-linux"
        "aarch64-linux"
        "aarch64-darwin"
      ];
    in
    {
      packages = forAllSystems (
        system: import ./nix/packages { pkgs = nixpkgs.legacyPackages.${system}; }
      );

      devShells = forAllSystems (
        system: import ./nix/devShells { pkgs = nixpkgs.legacyPackages.${system}; }
      );

      formatter = forAllSystems (
        system: import ./nix/formatter { pkgs = nixpkgs.legacyPackages.${system}; }
      );

      checks = forAllSystems (
        system:
        let
          prefix = name: lib.mapAttrs' (n: lib.nameValuePair "${name}-${n}");
        in
        import ./nix/checks {
          pkgs = nixpkgs.legacyPackages.${system};
          inspector = self.packages.${system}.default;
        }
        // prefix "package" self.packages.${system}
        // prefix "devShell" self.devShells.${system}
        // {
          treefmt = self.formatter.${system}.check self;
        }
      );
    };
}
