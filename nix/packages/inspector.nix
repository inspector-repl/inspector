{
  stdenv,
  pkgs,
  lib,
  cmake,
  ninja,
  pkg-config,
  jsoncpp,
  llvmPackages,
  llvm,
  pythonEnv,
  libffi,
}:

stdenv.mkDerivation {
  pname = "inspector";
  version = "0.1.0";

  src = ../..;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    pythonEnv
    (lib.hiPrio pkgs.buildPackages.clang-tools)
  ];

  buildInputs = [
    jsoncpp
    llvmPackages.clang-unwrapped.dev
    llvmPackages.clang-unwrapped.lib
    llvm.dev
  ]
  ++ lib.optionals stdenv.isDarwin [
    libffi
  ];

  cmakeFlags = [
    "-DCLANG_LIBDIR=${lib.getLib llvmPackages.clang-unwrapped}/lib"
  ];
}
