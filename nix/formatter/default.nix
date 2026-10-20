{ pkgs }:
pkgs.treefmt.withConfig {
  configFile = ../../treefmt.toml;
  runtimeInputs = [
    pkgs.llvmPackages_latest.clang-tools
    pkgs.cmake-format
    pkgs.nixfmt
    pkgs.shellcheck
  ];
}
