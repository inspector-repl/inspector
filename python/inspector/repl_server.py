import json
import socket
from collections.abc import Iterator
from typing import Any

from .repl import Repl
from .socket_path import get_socket_directory


def read_message(io: socket.socket) -> Iterator[str]:
    partial_line = b""
    while True:
        try:
            buf = io.recv(8192)
        except ConnectionResetError:
            break
        if not buf:
            break
        partial_line += buf
        lines = partial_line.split(b"\0")
        partial_line = lines.pop()
        for line in lines:
            yield line.decode("utf-8")
    if partial_line:
        yield partial_line.decode("utf-8")


def process_clients(_args: Any) -> None:
    socket_dir = get_socket_directory()
    # Private dir: the fallback base (/tmp) is shared between users.
    socket_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
    socket_path = socket_dir / "sock"
    socket_path.unlink(missing_ok=True)

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    with s:
        s.bind(str(socket_path))
        s.listen()
        try:
            while True:
                print(
                    f"Waiting for a program to reach INSPECTOR ({socket_path})... "
                    "Ctrl-C to exit"
                )
                conn, _addr = s.accept()
                with conn:
                    messages = read_message(conn)
                    try:
                        spec = json.loads(next(messages))
                    except StopIteration:
                        continue
                    repl = Repl(
                        messages,
                        conn,
                        spec["file"],
                        spec["line"],
                        spec.get("locals", ""),
                    )
                    repl.display_surrounding_code()
                    repl.run()
        except KeyboardInterrupt:
            print()
        finally:
            socket_path.unlink(missing_ok=True)
