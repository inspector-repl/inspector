{ inputs, ... }:
{
  imports = [
    inputs.treefmt-nix.flakeModule
  ];

  perSystem =
    { ... }:
    {
      treefmt = {
        projectRootFile = "flake.nix";
        settings.global.excludes = [ "third_party/**" ];
        programs = {
          nixfmt.enable = true;
          clang-format.enable = true;
          cmake-format.enable = true;
          shellcheck.enable = true;
        };
      };
    };
}
