import argparse

from .config import INCLUDE_PATH, LIBRARY_PATH

INSPECTOR_MACRO = "-DINSPECTOR=<inspector/__FILE__-__LINE__>"


def print_cflags(args: argparse.Namespace) -> None:
    print(
        f"-I .inspector-includes -I{INCLUDE_PATH} {INSPECTOR_MACRO} "
        f"-Wl,-rpath,{LIBRARY_PATH} -L{LIBRARY_PATH} -linspector"
    )
