#!/usr/bin/env python3
"""Drive an instrumented program through the REPL socket protocol (no tty).

usage: e2e.py <program>
"""

import json
import os
import socket
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, XDG_RUNTIME_DIR=tmp, HOME=tmp)
        sock_dir = Path(tmp) / "inspector"
        sock_dir.mkdir()
        srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        srv.bind(str(sock_dir / "sock"))
        srv.listen()
        srv.settimeout(120)
        proc = subprocess.Popen([sys.argv[1]], env=env)
        conn, _ = srv.accept()
        conn.settimeout(120)
        buf = b""

        def recv() -> dict[str, object]:
            nonlocal buf
            while b"\0" not in buf:
                chunk = conn.recv(8192)
                if not chunk:
                    raise SystemExit(f"program hung up, got: {buf!r}")
                buf += chunk
            msg, buf = buf.split(b"\0", 1)
            return json.loads(msg)  # type: ignore[no-any-return]

        def ask(stmt: str) -> str:
            conn.sendall(json.dumps({"input": stmt}).encode() + b"\0")
            return str(recv()["value"])

        loc = recv()
        print("location:", loc)
        checks = {"a + 41": "42", "b.size()": "12"}
        for stmt, want in checks.items():
            got = ask(stmt)
            print(f"{stmt} => {got}")
            if want not in got:
                raise SystemExit(f"expected {want!r} in {got!r}")
        ask(".quit")
        if proc.wait(timeout=60) != 0:
            raise SystemExit(f"program exited with {proc.returncode}")
    print("OK")


if __name__ == "__main__":
    main()
