{ pkgs }:

pkgs.python3.pkgs.buildPythonPackage rec {
  pname = "types_pygments";
  version = "2.19.0.20250809";

  src = pkgs.fetchPypi {
    inherit pname version;
    hash = "sha256-ATZv2T73PHkubuFkmNOr96GE8WJLULd/lQakfthZdMI=";
  };

  format = "setuptools";

  propagatedBuildInputs = [ ];

  doCheck = false;

  pythonImportsCheck = [ "pygments-stubs" ];
}
