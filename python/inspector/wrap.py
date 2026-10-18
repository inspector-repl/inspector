"""Compiler wrapper: makes `#include INSPECTOR` work in any build system.

CXX="inspector-c++"              make / autotools / meson
CMAKE_CXX_COMPILER_LAUNCHER=inspector;wrap
inspector wrap g++ -c foo.cpp    explicit
"""

import os
import sys
from dataclasses import dataclass, field
from pathlib import Path

from .config import INCLUDE_PATH, LIBRARY_PATH
from .prebuild import DEFAULT_OUTDIR, generate

SOURCE_SUFFIXES = (".cpp", ".cc", ".cxx", ".c++", ".C", ".cp", ".CPP")

# Options whose value is a separate argv entry.
_ARG_FLAGS = {
    "-o",
    "-I",
    "-D",
    "-U",
    "-include",
    "-imacros",
    "-isystem",
    "-iquote",
    "-idirafter",
    "-isysroot",
    "-x",
    "-MF",
    "-MT",
    "-MQ",
    "-Xclang",
    "-target",
    "--target",
    "-arch",
    "-L",
    "-l",
}
# Output/dependency options irrelevant (or harmful) for libclang parsing.
_DROP = {"-o", "-MF", "-MT", "-MQ", "-c", "-S", "-E", "-MD", "-MMD", "-M", "-MM"}


@dataclass
class Split:
    sources: list[str] = field(default_factory=list)
    flags: list[str] = field(default_factory=list)
    compile_only: bool = False


def split_command(args: list[str]) -> Split:
    s = Split()
    it = iter(args)
    for a in it:
        if a in ("-c", "-S", "-E", "-M", "-MM", "-fsyntax-only"):
            s.compile_only = True
        if a in _ARG_FLAGS:
            v = next(it, "")
            if a not in _DROP:
                s.flags += [a, v]
        elif a.startswith(("-", "@")):
            if a not in _DROP:
                s.flags.append(a)
        elif a.endswith(SOURCE_SUFFIXES):
            s.sources.append(a)
    return s


def wrapped_argv(compiler: str, args: list[str]) -> list[str]:
    split = split_command(args)
    outdir = Path(os.environ.get("INSPECTOR_INCLUDE_DIR", DEFAULT_OUTDIR)).absolute()
    for src in split.sources:
        generate(src, split.flags, outdir, compiler)
    extra: list[str] = []
    if split.sources:
        extra += [
            f"-I{outdir}",
            f"-I{INCLUDE_PATH}",
            "-DINSPECTOR=<inspector/__FILE__-__LINE__>",
        ]
    # CMake links via its own rule (inspector_enable adds the library).
    link = not split.compile_only and not os.environ.get("INSPECTOR_NO_LINK")
    tail = [f"-L{LIBRARY_PATH}", f"-Wl,-rpath,{LIBRARY_PATH}", "-linspector"]
    return [compiler, *extra, *args, *(tail if link else [])]


def _exec(compiler: str, args: list[str]) -> None:
    argv = wrapped_argv(compiler, args)
    os.execvp(argv[0], argv)


def wrap_main(args: list[str]) -> None:
    if not args:
        sys.exit("usage: inspector wrap <compiler> [args...]")
    _exec(args[0], args[1:])


def cxx_main() -> None:
    _exec(os.environ.get("INSPECTOR_CXX", "c++"), sys.argv[1:])
