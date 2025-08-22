#!/usr/bin/env python

from .repl import Repl
from .socket_path import get_socket_directory
import socket
import json


def read_message(input):
    partial_line = b""
    while True:
        buf = input.recv(8192)
        if not buf:
            break
        partial_line += buf
        lines = partial_line.split(b"\0")
        partial_line = lines.pop()
        for line in lines:
            yield line.decode("ascii")
    if partial_line:
        yield partial_line.decode("ascii")


def process_clients(args):
    # Get platform-specific socket directory
    socket_dir = get_socket_directory()

    # Create socket directory if it doesn't exist
    socket_dir.mkdir(parents=True, exist_ok=True)

    # Socket path
    socket_path = socket_dir / "sock"

    # Remove existing socket if it exists
    if socket_path.exists():
        socket_path.unlink()

    # Create Unix socket
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    with s:
        s.bind(str(socket_path))
        s.listen()
        print(f"Listening on Unix socket: {socket_path}")

        try:
            while True:
                conn, _addr = s.accept()
                with conn:
                    input_msg = read_message(conn)
                    file_spec = json.loads(next(input_msg))
                    file_path, line_number = file_spec["file"], file_spec["line"]
                    repl = Repl(input_msg, conn, file_path, line_number)
                    repl.display_surrounding_code()
                    repl.run()
        finally:
            # Clean up socket on exit
            if socket_path.exists():
                socket_path.unlink()
