{
  stdenv,
  lib,
  cmake,
  ninja,
  llvmPackages,
  llvm,
  python3,
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
      ../../pyproject.toml
      ../../include
      ../../lib
      ../../python
      ../../test
      ../../third_party
      ../../tools
    ];
  };

  nativeBuildInputs = [
    cmake
    ninja
    (python3.withPackages (ps: [
      ps.hatchling
      ps.pip
      ps.libclang
    ]))
    python3.pkgs.wrapPython
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

  pythonPath = with python3.pkgs; [
    libclang
  ];

  cmakeFlags = [
    "-DCLANG_RESOURCE_DIR=${lib.getLib llvmPackages.clang-unwrapped}/lib/clang/${lib.versions.major llvmPackages.clang-unwrapped.version}"
  ];

  doCheck = true;

  postFixup = ''
    wrapPythonPrograms
  '';

  meta.mainProgram = "inspector";
}
