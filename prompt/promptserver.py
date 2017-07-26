#!/usr/bin/env python

from repl import Repl
import socket


def main():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("localhost", 5000))
    s.listen()
    while True:
        conn, addr = s.accept()
        input = conn.makefile("r", encoding="utf-8", newline="\n")
        output = conn.makefile("w", encoding="utf-8", newline="\n")
        file_spec = input.readline()
        file_path, line_number = file_spec.split(":")
        repl = Repl(input, output, file_path, line_number)
        repl.run_interpreter_loop()


if __name__ == "__main__":
    main()
