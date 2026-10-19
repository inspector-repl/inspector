{
  stdenv,
  lib,
  cmake,
  ninja,
  llvmPackages,
  llvm,
  libffi,
  gtest,
}:

stdenv.mkDerivation {
  pname = "inspector";
  version = "0.1.0";

  src = lib.fileset.toSource {
    root = ../..;
    fileset = lib.fileset.unions [
      ../../CMakeLists.txt
      ../../.clang-tidy
      ../../cmake
      ../../include
      ../../lib
      ../../test
      ../../third_party
      ../../tools
    ];
  };

  nativeBuildInputs = [
    cmake
    ninja
  ];

  buildInputs = [
    llvmPackages.clang-unwrapped.dev
    llvmPackages.clang-unwrapped.lib
    llvm.dev
    gtest
  ]
  ++ lib.optionals stdenv.hostPlatform.isDarwin [
    libffi
  ];

  cmakeFlags = [
    "-DCLANG_RESOURCE_DIR=${lib.getLib llvmPackages.clang-unwrapped}/lib/clang/${lib.versions.major llvmPackages.clang-unwrapped.version}"
  ];

  doCheck = true;

  meta.mainProgram = "inspector";
}
