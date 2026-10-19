#!/usr/bin/env python3
"""Drive inspector-repl through a pseudo terminal against a real program.

usage: repl_tty.py <inspector-repl> <program>
"""

import os
import pty
import re
import select
import subprocess
import sys
import tempfile
import time

ANSI = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]")


class Tty:
    def __init__(self, argv: list[str], env: dict[str, str]) -> None:
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.execve(argv[0], argv, env)
        self.seen = b""

    def expect(self, pattern: bytes, timeout: float = 60) -> bytes:
        deadline = time.monotonic() + timeout
        while pattern not in ANSI.sub(b"", self.seen):
            left = deadline - time.monotonic()
            if left <= 0 or not select.select([self.fd], [], [], left)[0]:
                raise SystemExit(f"timeout waiting for {pattern!r}: {self.seen!r}")
            try:
                self.seen += os.read(self.fd, 4096)
            except OSError as e:
                raise SystemExit(f"tty closed: {self.seen!r}") from e
        out = ANSI.sub(b"", self.seen)
        self.seen = b""
        return out

    def send(self, data: bytes) -> None:
        os.write(self.fd, data)


def main() -> None:
    repl, program = sys.argv[1:3]
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, XDG_RUNTIME_DIR=tmp, XDG_STATE_HOME=tmp, TERM="xterm")
        tty = Tty([repl], env)
        tty.expect(b"Waiting")
        proc = subprocess.Popen([program], env=env)
        tty.expect(b"[0]")
        # Tab completion of a library member, answered by the program.
        tty.send(b"b.si\t")
        tty.expect(b"b.size")
        tty.send(b"()\r")
        out = tty.expect(b"[1]")
        if b"(unsigned long) 12" not in out:
            raise SystemExit(f"completion + eval failed: {out!r}")
        # Enter inside open brackets continues the input.
        tty.send(b"int z = (1 +\r2);\r")
        tty.expect(b"[2]")
        tty.send(b"z\r")
        out = tty.expect(b"[3]")
        if b"(int) 3" not in out:
            raise SystemExit(f"multi-line input failed: {out!r}")
        tty.send(b".continue\r")
        tty.expect(b"continuing program")
        if proc.wait(timeout=60) != 0:
            raise SystemExit(f"program exited with {proc.returncode}")
        os.kill(tty.pid, 2)
        os.waitpid(tty.pid, 0)
    print("OK")


if __name__ == "__main__":
    main()
