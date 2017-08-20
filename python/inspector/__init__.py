import sys
import argparse

import sys
import argparse

from .prebuild import generate_header_for_file
from .cflags import print_cflags
from . import repl_server


def parse_options(argv):
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers()

    prebuild_parser = subparsers.add_parser("prebuild")
    prebuild_parser.add_argument("sourcefile")
    prebuild_parser.set_defaults(func=generate_header_for_file)

    cflags_parser = subparsers.add_parser("print-cflags")
    cflags_parser.set_defaults(func=print_cflags)

    repl_parser = subparsers.add_parser("repl")
    repl_parser.set_defaults(func=repl_server.process_clients)

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
