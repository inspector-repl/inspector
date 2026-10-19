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
            return str(recv()["value"]).strip()

        def complete(text: str) -> list[str]:
            conn.sendall(json.dumps({"complete": text}).encode() + b"\0")
            reply = recv()
            names = reply["completions"]
            assert isinstance(names, list)
            print(f"complete {text!r} => prefix {reply['prefix']!r} {names[:8]}")
            return [str(n) for n in names]

        loc = recv()
        print("location:", loc)
        if "& a = *(" not in str(loc.get("locals")):
            raise SystemExit(f"locals missing: {loc}")
        ready = recv()
        if not ready.get("ready") or ready.get("error"):
            raise SystemExit(f"interpreter failed to start: {ready}")
        checks = {
            "a + 41": "42",
            "b.size()": "12",
            "b": '"Hello world!"',
            "int c = a * 3;": "",
            "c": "3",
            "undefined_name + 1": "undeclared identifier",
        }
        # Scoping and type-spelling cases only exist in test/test.cpp.
        if "& arr = *(" in str(loc.get("locals")):
            checks["arr[1] + *ptr"] = "9"
            checks["map"] = '(std::map<std::string, int> &) {{"k", 6}}'
            checks["throw 1;"] = "uncaught exception"
            # LLVM 22's JIT on Darwin cannot unwind exceptions thrown from
            # inside libc++ through JIT frames; fixed in LLVM 23.
            if (
                sys.platform != "darwin"
                or int(os.environ.get("INSPECTOR_LLVM_MAJOR", "23")) >= 23
            ):
                checks["b.at(99)"] = "uncaught exception"
        for stmt, want in checks.items():
            got = ask(stmt)
            print(f"{stmt} => {got!r}")
            if want not in got or (not want and got):
                raise SystemExit(f"expected {want!r} in {got!r}")
        # Locals, members of library types and names declared in the REPL.
        for text, want in {"b.si": "size", "c + b.si": "size", "c": "c"}.items():
            if want not in complete(text):
                raise SystemExit(f"expected {want!r} completing {text!r}")
        if any(n.startswith("_") for n in complete("b.")):
            raise SystemExit("reserved names leaked into completions")
        ask(".quit")
        if proc.wait(timeout=60) != 0:
            raise SystemExit(f"program exited with {proc.returncode}")

    # Without a listening REPL the program must keep running.
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, XDG_RUNTIME_DIR=tmp, HOME=tmp)
        r = subprocess.run(
            [sys.argv[1]], env=env, capture_output=True, timeout=60, check=False
        )
        if r.returncode != 0 or b"continuing" not in r.stderr:
            raise SystemExit(f"no-listener run failed: {r}")
    print("OK")


if __name__ == "__main__":
    main()
