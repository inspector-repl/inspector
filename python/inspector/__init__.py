
import sys
import argparse

import sys
import argparse

from .prebuild import generate_header_for_file
from .cflags import print_cflags


def parse_options(argv):
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers()

    parser_prebuild = subparsers.add_parser("prebuild")
    parser_prebuild.add_argument("sourcefile")
    parser_prebuild.set_defaults(func=generate_header_for_file)

    parser_cflags = subparsers.add_parser("print-cflags")
    parser_cflags.set_defaults(func=print_cflags)
    args = parser.parse_args(argv)

    if 'func' not in args:
        parser.print_help()
        sys.exit(1)
    return args


def _real_main(argv):
    args = parse_options(argv)
    args.func(args)


def main():
    try:
        _real_main(sys.argv[1:])
    except KeyboardInterrupt:
        sys.exit("Interrupted by user")
