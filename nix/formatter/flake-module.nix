{ inputs, ... }:
{
  imports = [
    inputs.treefmt-nix.flakeModule
  ];

  perSystem =
    { pkgs, ... }:
    {
      treefmt = {
        projectRootFile = "flake.nix";
        settings.global.excludes = [ "third_party/**" ];
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
                  pkgs.python3.pkgs.libclang
                ];
              };
            };
          };
        };
      };
    };
}
