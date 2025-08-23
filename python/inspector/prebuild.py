#!/usr/bin/env python
import os
import argparse
from typing import Iterator
from clang.cindex import Index, TranslationUnit, CursorKind, Cursor, SourceLocation  # type: ignore[import-untyped]
from .config import INCLUDE_PATH


def _escape_c_string(string: str) -> str:
    result = ""
    for c in string:
        if not (32 <= ord(c) < 127) or c == "\\" or c == '"':
            result += "\\%03o" % ord(c)
        else:
            result += c
    return '"' + result + '"'


def _find_inspector_callsites(
    node: Cursor, closure: list[Cursor], in_function: bool
) -> tuple[list[SourceLocation], list[list[Cursor]]]:
    """
    Macro invocations are not aligned with the remaining ast.
    - We need the macro invocation to get source line, where INSPECTOR was included
    - To get the position in the AST, we look for a our dummy header, this
      gives us the closure with all defined variables and symbols
    We can combine both information with a simple zip(a,b)
    """
    include_locations = []
    closures = []
    # reset closure outside of functions
    if not in_function:
        closure = []
    if node.kind == CursorKind.MACRO_INSTANTIATION and node.spelling == "INSPECTOR":
        include_locations.append(node.location)
    elif in_function:
        if (
            node.kind == CursorKind.COMPOUND_STMT
            and node.location is not None
            and node.location.file.name.endswith("inspector/dummy.h")
        ):
            closures.append(closure)
        elif node.kind == CursorKind.VAR_DECL or node.kind == CursorKind.PARM_DECL:
            closure.append(node)

    for c in node.get_children():
        in_function = in_function or node.kind == CursorKind.FUNCTION_DECL
        include_locations_, closures_ = _find_inspector_callsites(
            c, closure, in_function
        )
        include_locations.extend(include_locations_)
        closures.extend(closures_)

    return include_locations, closures


def find_inspector_callsites(
    nodes: Cursor,
) -> Iterator[tuple[SourceLocation, list[Cursor]]]:
    include_locations, closures = _find_inspector_callsites(nodes, [], False)
    return zip(include_locations, closures)


INSPECTOR_REPL_PRELUDE = [
    "#define INSIDE_CLING 1",
    "#define INSPECTOR <inspector/dummy.h>",
    # for C mode this needs to be put in `extern "C" {}` block
    '''#include \\"{file}\\"''',
]

INSPECTOR_VARIABLES_TEMPLATE = """{type}& {name} = *({type}*) %p;"""

INSPECTOR_HEADER_TEMPLATE = """
#ifndef INSIDE_CLING /* yo dawg, I heard you like repls */
{{
    static const char* __inspector_includes[] = {{
        {includes},
        NULL
    }};
    
    void inspectorRunRepl(const char* path, unsigned lineNumber, const char* clingDeclare, const char* clingContext, const char* const* clingIncludes, ...);
    inspectorRunRepl(
        /* file         */ "{file}",     
        /* line         */  {line},      
        /* declare      */ "{declare}",  
        /* prelude      */ "{prelude}",
        /* includes     */ __inspector_includes,
        /* pointerlist  */  {pointerlist}
    );
}}
#endif
"""


def write_header(
    location: SourceLocation, closure: list[Cursor], include_paths: list[str]
) -> None:
    file_name = location.file.name
    # expanded form of  __FILE__ __LINE__
    path = '"{}"-{}'.format(file_name, location.line)
    header_file = os.path.join(".inspector-includes", "inspector", path)
    os.makedirs(os.path.dirname(header_file), exist_ok=True)
    prelude = []
    pointerlist = []
    for node in closure:
        prelude.append(
            INSPECTOR_VARIABLES_TEMPLATE.format(
                type=node.type.spelling, name=node.spelling
            )
        )
        pointerlist.append("&{variable}".format(variable=node.spelling))
    with open(header_file, "w+") as f:
        print(header_file)
        data = dict(
            file=file_name,
            line=location.line,
            declare="\\n".join(INSPECTOR_REPL_PRELUDE).format(file=file_name),
            prelude="\\n".join(prelude),
            includes=",\n        ".join([f'"{flag}"' for flag in include_paths])
            if include_paths
            else "",
            pointerlist=", ".join(pointerlist),
        )
        f.write(INSPECTOR_HEADER_TEMPLATE.format(**data))


def generate_header_for_file(args: argparse.Namespace) -> None:
    import subprocess

    index = Index.create()
    cflags = [
        "-std=c++17",  # Need C++ support to parse std::string etc
        "-I{include}".format(include=INCLUDE_PATH),
        "-DINSPECTOR=<inspector/dummy.h>",
    ]

    # Try to get C++ include paths from the compiler
    # Use CXX if set, otherwise CC, otherwise default to clang++
    compiler = os.getenv("CXX", os.getenv("CC", "clang++"))
    try:
        result = subprocess.run(
            [compiler, "-E", "-x", "c++", "-", "-v"],
            stdin=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        if result.stderr:
            in_search = False
            for line in result.stderr.split("\n"):
                if "#include <...> search starts here:" in line:
                    in_search = True
                elif "End of search list." in line:
                    in_search = False
                elif in_search and line.strip():
                    include_path = line.strip()
                    if os.path.isdir(include_path):
                        cflags.append(f"-I{include_path}")
    except Exception:
        pass  # If we can't get the paths, continue with existing flags

    for flag in os.getenv("NIX_CFLAGS_COMPILE", "").split(" "):
        if len(flag) > 0:
            cflags.append(flag)

    tu = index.parse(
        args.sourcefile,
        cflags,
        options=TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD,
    )
    if not tu:
        raise RuntimeError("unable to load input")

    # Collect include paths that were successfully used (keep the full -I flag)
    include_paths = []
    for flag in cflags:
        if flag.startswith("-I"):
            include_paths.append(flag)

    callsites = find_inspector_callsites(tu.cursor)
    print("Writing include files to:")
    for location, closure in callsites:
        write_header(location, closure, include_paths)
