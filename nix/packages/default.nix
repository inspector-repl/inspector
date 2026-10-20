{ pkgs }:
let
  llvmPackages = pkgs.llvmPackages_latest;
  inspector = pkgs.callPackage ./inspector.nix {
    inherit llvmPackages;
    inherit (llvmPackages) stdenv llvm;
  };
in
{
  default = inspector;
  inherit inspector;
}
