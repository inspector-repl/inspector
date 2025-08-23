{ inputs, ... }:
{
  imports = [
    inputs.treefmt-nix.flakeModule
  ];

  perSystem =
    { pkgs, ... }:
    let
      types-pygments = pkgs.callPackage ./types-pygments.nix { };
    in
    {
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
}
