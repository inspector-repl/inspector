{
  perSystem =
    { pkgs, ... }:
    let
      llvmPackages = pkgs.llvmPackages_git;
      stdenv = llvmPackages.stdenv;
      llvm = llvmPackages.llvm;

      pythonEnv = pkgs.python3.withPackages (
        ps: with ps; [
          prompt-toolkit
          pygments
          setuptools
          libclang
        ]
      );

      inspector = pkgs.callPackage ./inspector.nix {
        inherit
          stdenv
          llvmPackages
          llvm
          pythonEnv
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
