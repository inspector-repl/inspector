import argparse
import shlex
import sys

from . import repl_server
from .cflags import print_cflags
from .prebuild import generate_header_for_file
from .wrap import wrap_main


def parse_options(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers()

    prebuild_parser = subparsers.add_parser("prebuild")
    prebuild_parser.add_argument("sourcefiles", nargs="*")
    prebuild_parser.add_argument(
        "-p", "--compile-commands", help="prebuild every file in compile_commands.json"
    )
    prebuild_parser.add_argument("-o", "--outdir", default=".inspector-includes")
    prebuild_parser.add_argument("-j", "--jobs", type=int, default=None)
    prebuild_parser.add_argument(
        "--cflags", type=shlex.split, default=[], help="flags used to compile"
    )
    prebuild_parser.set_defaults(func=generate_header_for_file)

    cflags_parser = subparsers.add_parser("print-cflags")
    cflags_parser.set_defaults(func=print_cflags)

    subparsers.add_parser("wrap", help="wrap a compiler: inspector wrap g++ ...")

    repl_parser = subparsers.add_parser("repl")
    repl_parser.set_defaults(func=repl_server.process_clients)

    args = parser.parse_args(argv)

    if "func" not in args:
        parser.print_help()
        sys.exit(1)
    return args


def _real_main(argv: list[str]) -> None:
    # wrap forwards arbitrary compiler flags, argparse must not touch them
    if argv[:1] == ["wrap"]:
        wrap_main(argv[1:])
        return
    args = parse_options(argv)
    args.func(args)


def main() -> None:
    try:
        _real_main(sys.argv[1:])
    except KeyboardInterrupt:
        sys.exit("Interrupted by user")
