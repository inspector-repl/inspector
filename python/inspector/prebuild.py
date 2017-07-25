#!/usr/bin/env python
import sys
import os
from clang.cindex import Index, TranslationUnit, CursorKind

INSPECTOR_MACRO = '-DINSPECTOR=<inspector/__FILE__ __LINE__>'


def find_inspector_macros(node):
    locations = []
    if node.kind == CursorKind.MACRO_INSTANTIATION and \
            node.spelling == "INSPECTOR":
        locations.append(node.location)
    for c in node.get_children():
        locations.extend(find_inspector_macros(c))
    return locations


INSPECTOR_HEADER_TEMPLATE = """
void inspectorRunRepl(const char* path, unsigned lineNumber, const char* clingContext, ...);
inspectorRunRepl("{file}", {line}, "#include {file}");
"""


def write_header(location):
    file_name = location.file.name
    # expanded form of  __FILE__ __LINE__
    path = "\"{}\"{}".format(file_name, location.line)
    header_file = os.path.join(".inspector-includes", "inspector", path)
    os.makedirs(os.path.dirname(header_file), exist_ok=True)
    with open(header_file, "w+") as f:
        data = dict(file=file_name, line=location.line)
        f.write(INSPECTOR_HEADER_TEMPLATE.format(**data))


def generate_header_for_file(args):
    index = Index.create()
    tu = index.parse(
        args.sourcefile, [INSPECTOR_MACRO],
        options=TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD)
    if not tu:
        parser.error("unable to load input")

    locations = find_inspector_macros(tu.cursor)
    for location in locations:
        write_header(location)


if __name__ == '__main__':
    main()
