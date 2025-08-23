{
  perSystem =
    { pkgs, ... }:
    let
      llvmPackages = pkgs.llvmPackages_git;
      stdenv = llvmPackages.stdenv;
      llvm = llvmPackages.llvm;

      inspector = pkgs.callPackage ./inspector.nix {
        inherit
          stdenv
          llvmPackages
          llvm
          ;
        python3 = pkgs.python3;
      };
    in
    {
      packages = {
        default = inspector;
        inherit inspector;
      };
    };
}
