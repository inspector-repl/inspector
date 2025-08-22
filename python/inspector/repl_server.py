#!/usr/bin/env python

from .repl import Repl
import socket
import json
import os
from pathlib import Path


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
    # Get XDG_RUNTIME_DIR
    xdg_runtime_dir = os.environ.get("XDG_RUNTIME_DIR")
    if not xdg_runtime_dir:
        raise RuntimeError("XDG_RUNTIME_DIR not set")

    # Create socket directory if it doesn't exist
    socket_dir = Path(xdg_runtime_dir) / "inspector"
    socket_dir.mkdir(parents=True, exist_ok=True)

    # Socket path
    socket_path = socket_dir / "sock"

    # Remove existing socket if it exists
    if socket_path.exists():
        socket_path.unlink()

    # Create Unix socket
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.bind(str(socket_path))
    s.listen()
    print(f"Listening on Unix socket: {socket_path}")

    try:
        while True:
            conn, addr = s.accept()
            input = read_message(conn)
            output = conn
            file_spec = json.loads(next(input))
            file_path, line_number = file_spec["file"], file_spec["line"]
            repl = Repl(input, output, file_path, line_number)
            repl.display_surrounding_code()
            repl.run()
    finally:
        # Clean up socket on exit
        if socket_path.exists():
            socket_path.unlink()


if __name__ == "__main__":
    process_clients()
