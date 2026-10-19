import argparse
import json
import os
import subprocess
from collections.abc import Iterator, Sequence
from concurrent.futures import ProcessPoolExecutor
from functools import cache
from itertools import pairwise
from pathlib import Path

from clang.cindex import (  # type: ignore[import-untyped]
    Cursor,
    CursorKind,
    Index,
    SourceLocation,
    TranslationUnit,
)

from .config import INCLUDE_PATH

DEFAULT_OUTDIR = ".inspector-includes"


def _escape_c_string(string: str) -> str:
    result = ""
    for c in string:
        if not (32 <= ord(c) < 127) or c == "\\" or c == '"':
            result += f"\\{ord(c):03o}"
        else:
            result += c
    return result


# Locals of enclosing functions are not reachable from inside these.
_SCOPE_BARRIERS = {
    CursorKind.FUNCTION_DECL,
    CursorKind.CXX_METHOD,
    CursorKind.CONSTRUCTOR,
    CursorKind.DESTRUCTOR,
    CursorKind.CONVERSION_FUNCTION,
    CursorKind.FUNCTION_TEMPLATE,
    CursorKind.LAMBDA_EXPR,
    CursorKind.CLASS_DECL,
    CursorKind.STRUCT_DECL,
}


def _is_breakpoint(node: Cursor) -> bool:
    return (
        node.kind == CursorKind.COMPOUND_STMT
        and node.location.file is not None
        and node.location.file.name.endswith("inspector/dummy.h")
    )


def _declared(node: Cursor) -> Iterator[Cursor]:
    if node.kind in (CursorKind.VAR_DECL, CursorKind.PARM_DECL):
        yield node
    elif node.kind == CursorKind.DECL_STMT:
        for child in node.get_children():
            yield from _declared(child)


def _visible(path: list[Cursor]) -> list[Cursor]:
    """Variables declared before `path[-1]` in each enclosing scope."""
    found: list[Cursor] = []
    for parent, child in pairwise(path):
        if parent.kind in _SCOPE_BARRIERS:
            found = []
        for sibling in parent.get_children():
            if sibling == child:
                break
            found.extend(_declared(sibling))
    return found


def _breakpoints(node: Cursor, path: list[Cursor]) -> Iterator[list[Cursor]]:
    path = [*path, node]
    if _is_breakpoint(node):
        yield _visible(path)
        return
    for child in node.get_children():
        yield from _breakpoints(child, path)


def find_inspector_callsites(
    tu: TranslationUnit,
) -> Iterator[tuple[SourceLocation, list[Cursor]]]:
    # Macro expansions aren't part of the AST: the expansion gives the line,
    # the dummy header's block gives the scope. Both appear in source order.
    locations = [
        c.location
        for c in tu.cursor.get_children()
        if c.kind == CursorKind.MACRO_INSTANTIATION and c.spelling == "INSPECTOR"
    ]
    return zip(locations, _breakpoints(tu.cursor, []))


# Spelling the type as a template argument keeps arrays and function
# pointers valid; canonical types avoid libstdc++-internal aliases.
INSPECTOR_VARIABLES_TEMPLATE = (
    "inspector_repl::type<{type}>& {name} = *(inspector_repl::type<{type}>*) %p;"
)

INSPECTOR_HEADER_TEMPLATE = """
#ifndef INSIDE_CLING /* yo dawg, I heard you like repls */
{{
    static const char* const __inspector_includes[] = {{
        {includes}
        0
    }};
    const void* const __inspector_addresses[] = {{ {pointerlist} }};
    /* C linkage comes from <inspector/repl.h>, force-included by the build. */
    inspectorRunRepl(
        /* file         */ "{file}",
        /* line         */  {line},
        /* declare      */ "{declare}",
        /* prelude      */ "{prelude}",
        /* includes     */ __inspector_includes,
        /* addresses    */ __inspector_addresses
    );
}}
#endif
"""


def write_header(
    outdir: Path,
    location: SourceLocation,
    closure: list[Cursor],
    repl_flags: list[str],
) -> Path:
    file_name = location.file.name
    abs_file = os.path.abspath(file_name)
    # Must match what `INSPECTOR` (= <inspector/__FILE__-__LINE__>) expands to.
    header_file = outdir / "inspector" / f'"{file_name}"-{location.line}'
    header_file.parent.mkdir(parents=True, exist_ok=True)
    prelude = []
    pointerlist = []
    for node in closure:
        # Lambdas and unnamed types have no spelling the REPL could parse.
        if "(lambda at" in node.type.spelling or "(unnamed" in node.type.spelling:
            continue
        prelude.append(
            INSPECTOR_VARIABLES_TEMPLATE.format(
                type=node.type.get_canonical().spelling, name=node.spelling
            )
        )
        pointerlist.append(f"(const void*) &{node.spelling}")
    declare = "\n".join(
        [
            "#define INSIDE_CLING 1",
            "#define INSPECTOR <inspector/dummy.h>",
            f'#include "{abs_file}"',
        ]
    )
    content = INSPECTOR_HEADER_TEMPLATE.format(
        file=_escape_c_string(abs_file),
        line=location.line,
        declare=_escape_c_string(declare),
        prelude=_escape_c_string("\n".join(prelude)),
        includes="".join(f'"{_escape_c_string(f)}",\n        ' for f in repl_flags),
        pointerlist=", ".join(pointerlist) or "0",
    )
    # Keep mtime stable so build systems don't recompile needlessly.
    if not header_file.exists() or header_file.read_text() != content:
        header_file.write_text(content)
    return header_file


@cache
def compiler_include_paths(compiler: str) -> tuple[str, ...]:
    """System include dirs the real compiler uses (libstdc++, glibc, ...)."""
    try:
        result = subprocess.run(
            [compiler, "-E", "-x", "c++", "-", "-v"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
    except OSError:
        return ()
    paths = []
    in_search = False
    for line in result.stderr.splitlines():
        if "#include <...> search starts here:" in line:
            in_search = True
        elif "End of search list." in line:
            in_search = False
        elif in_search:
            path = line.strip().removesuffix(" (framework directory)")
            if os.path.isdir(path):
                paths.append(path)
    return tuple(paths)


# flags relevant to name lookup that the REPL needs to see the same code
_REPL_FLAG_PREFIXES = ("-I", "-D", "-U", "-std=", "-isystem", "-include")
_FLAGS_WITH_ARG = {"-I", "-D", "-U", "-isystem", "-include", "-iquote"}


def repl_flags_from(flags: Sequence[str]) -> list[str]:
    out: list[str] = []
    it = iter(flags)
    for flag in it:
        if flag in _FLAGS_WITH_ARG:
            arg = next(it, "")
            if flag in ("-I", "-isystem", "-iquote"):
                arg = os.path.abspath(arg)
            out += [flag, arg]
        elif flag.startswith(("-I", "-iquote")) and not flag.startswith("-isystem"):
            prefix = "-iquote" if flag.startswith("-iquote") else "-I"
            out.append(prefix + os.path.abspath(flag[len(prefix) :]))
        elif flag.startswith(_REPL_FLAG_PREFIXES):
            out.append(flag)
    return out


def mentions_inspector(source: str) -> bool:
    """Cheap pre-scan so unrelated files never pay for a libclang parse."""
    if os.environ.get("INSPECTOR_SCAN_ALL"):
        return True
    try:
        with open(source, "rb") as f:
            return b"INSPECTOR" in f.read()
    except OSError:
        return False


def generate(
    source: str,
    flags: Sequence[str],
    outdir: Path,
    compiler: str | None = None,
) -> list[Path]:
    """Parse `source` with `flags` and write a header per INSPECTOR callsite."""
    if not mentions_inspector(source):
        return []
    compiler = compiler or os.environ.get("CXX", "c++")
    system_includes = [f"-isystem{p}" for p in compiler_include_paths(compiler)]
    cflags = [
        "-x",
        "c++",
        *flags,
        f"-I{INCLUDE_PATH}",
        "-DINSPECTOR=<inspector/dummy.h>",
        *system_includes,
    ]
    tu = Index.create().parse(
        source, cflags, options=TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD
    )
    if not tu:
        raise RuntimeError(f"unable to parse {source}")
    repl_flags = repl_flags_from([*flags, f"-I{INCLUDE_PATH}"]) + system_includes
    return [
        write_header(outdir, loc, closure, repl_flags)
        for loc, closure in find_inspector_callsites(tu)
    ]


def _job(job: tuple[str, str, list[str], str, str | None]) -> list[Path]:
    directory, source, flags, outdir, compiler = job
    os.chdir(directory)
    return generate(source, flags, Path(outdir), compiler)


def _jobs_from_compdb(
    path: str, outdir: str
) -> Iterator[tuple[str, str, list[str], str, str | None]]:
    from .wrap import split_command

    with open(path) as f:
        entries = json.load(f)
    for entry in entries:
        argv = entry.get("arguments")
        if argv is None:
            import shlex

            argv = shlex.split(entry["command"])
        split = split_command(argv[1:])
        directory = entry["directory"]
        for src in split.sources:
            yield (
                directory,
                src,
                split.flags,
                os.path.join(directory, outdir),
                argv[0],
            )


def generate_header_for_file(args: argparse.Namespace) -> None:
    outdir = os.path.abspath(args.outdir)
    jobs: list[tuple[str, str, list[str], str, str | None]] = [
        (os.getcwd(), src, args.cflags, outdir, None) for src in args.sourcefiles
    ]
    if args.compile_commands:
        jobs += _jobs_from_compdb(args.compile_commands, args.outdir)
    if len(jobs) == 1:
        results = [_job(jobs[0])]
    else:
        with ProcessPoolExecutor(max_workers=args.jobs) as pool:
            results = list(pool.map(_job, jobs))
    for headers in results:
        for h in headers:
            print(h)
