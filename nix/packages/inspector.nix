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
  python3,
  libffi,
}:

stdenv.mkDerivation {
  pname = "inspector";
  version = "0.1.0";

  src = lib.fileset.toSource {
    root = ../..;
    fileset = lib.fileset.unions [
      ../../CMakeLists.txt
      ../../cmake
      ../../pyproject.toml
      ../../include
      ../../lib
      ../../python
      ../../test
    ];
  };

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    (python3.withPackages (ps: [
      ps.hatchling
      ps.pip
      ps.libclang
      ps.prompt-toolkit
      ps.pygments
    ]))
    python3.pkgs.wrapPython
    (lib.hiPrio pkgs.buildPackages.clang-tools)
  ];

  buildInputs = [
    jsoncpp
    llvmPackages.clang-unwrapped.dev
    llvmPackages.clang-unwrapped.lib
    llvm.dev
  ]
  ++ lib.optionals stdenv.hostPlatform.isDarwin [
    libffi
  ];

  pythonPath = with python3.pkgs; [
    prompt-toolkit
    pygments
    libclang
  ];

  cmakeFlags = [
    "-DCLANG_RESOURCE_DIR=${lib.getLib llvmPackages.clang-unwrapped}/lib/clang/${lib.versions.major llvmPackages.clang-unwrapped.version}"
  ];

  doCheck = true;

  postFixup = ''
    wrapPythonPrograms
  '';
}
