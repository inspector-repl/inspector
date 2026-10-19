{
  perSystem =
    { pkgs, ... }:
    let
      llvmPackages = pkgs.llvmPackages_latest;
      stdenv = llvmPackages.stdenv;
      llvm = llvmPackages.llvm;

      inspector = pkgs.callPackage ./inspector.nix {
        inherit
          stdenv
          llvmPackages
          llvm
          ;
      };
    in
    {
      packages = {
        default = inspector;
        inherit inspector;
      };
    };
}
