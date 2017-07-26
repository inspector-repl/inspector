
#!/usr/bin/env python

from repl import Repl
import socket
import json


def read_message(input):
    partial_line = b""
    while True:
        buf = input.recv(8192)
        if not buf: break
        partial_line += buf
        lines = partial_line.split(b'\0')
        partial_line = lines.pop()
        for line in lines:
            yield line.decode("ascii")
    if partial_line:
        yield partial_line.decode("ascii")


def main():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("localhost", 5000))
    s.listen()
    while True:
        conn, addr = s.accept()
        input = read_message(conn)
        output = conn
        file_spec = json.loads(next(input))
        file_path, line_number = file_spec["file"], file_spec["line"]
        repl = Repl(input, output, file_path, line_number)
        repl.display_surrounding_code()
        repl.run_interpreter_loop()


if __name__ == "__main__":
    main()
