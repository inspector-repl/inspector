import json
import os
import re
import socket
from collections.abc import Iterator
from pathlib import Path

import pygments
from prompt_toolkit import PromptSession
from prompt_toolkit.formatted_text import ANSI, HTML
from prompt_toolkit.history import FileHistory, History, InMemoryHistory
from prompt_toolkit.key_binding import KeyBindings, KeyPressEvent
from prompt_toolkit.lexers import PygmentsLexer
from prompt_toolkit.shortcuts import print_formatted_text
from pygments.formatters import TerminalFormatter
from pygments.lexers import CppLexer

from .clang_completer import ClangCompleter

HELP = """\
Type C++ statements or expressions; expressions print their value.
A trailing ';' suppresses the result. Multi-line input continues while
brackets are open.

  .locals      list variables captured at this breakpoint
  .where       show the surrounding source again
  .continue    resume the program (also .quit, Ctrl-D)
  .abort       terminate the program
  .help        this message

Ctrl-C clears the current line."""

_LOCAL_RE = re.compile(r"^(?P<type>.*)& (?P<name>\w+) = \*\(")


class ProgramGone(Exception):
    pass


def parse_locals(prelude: str) -> list[tuple[str, str]]:
    """Recover (type, name) pairs from the generated reference prelude."""
    out = []
    for line in prelude.splitlines():
        m = _LOCAL_RE.match(line)
        if m:
            out.append((m["type"], m["name"]))
    return out


def history_file() -> History:
    state = os.environ.get("XDG_STATE_HOME") or os.path.expanduser("~/.local/state")
    path = Path(state) / "inspector" / "history"
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        return FileHistory(str(path))
    except OSError:
        return InMemoryHistory()


def _open_brackets(text: str) -> int:
    depth = 0
    for c in re.sub(r'"(\\.|[^"\\])*"|\'(\\.|[^\'\\])*\'', "", text):
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
    return depth


def _key_bindings() -> KeyBindings:
    kb = KeyBindings()

    @kb.add("enter")
    def _(event: KeyPressEvent) -> None:
        buf = event.current_buffer
        if _open_brackets(buf.text) > 0:
            buf.insert_text("\n")
        else:
            buf.validate_and_handle()

    return kb


class Repl:
    def __init__(
        self,
        input: Iterator[str],
        output: socket.socket,
        file_path: str,
        line_number: int,
        locals_prelude: str = "",
    ) -> None:
        self.input = input
        self.output = output
        self.file_path = file_path
        self.line_number = self._inspector_line(file_path, line_number)
        self.locals = parse_locals(locals_prelude)
        self.statement_count = 0

    @staticmethod
    def _inspector_line(path: str, line: int) -> int:
        # libclang reports the line before the `#include INSPECTOR` directive.
        try:
            with open(path) as f:
                lines = f.readlines()
        except OSError:
            return line
        if line < len(lines) and "INSPECTOR" in lines[line]:
            return line + 1
        return line

    def get_code_context(self, filename: str, line_number: int) -> str:
        first = max(line_number - 5, 1)
        last = line_number + 3
        context = []
        try:
            with open(filename) as f:
                for i, line in enumerate(f, start=1):
                    if first <= i <= last:
                        context.append(line.rstrip())
                    if i > last:
                        break
        except OSError:
            pass
        banner = f"\033[1mFrom: {filename} @ line {line_number}:\033[0m\n"
        width = len(str(first + len(context)))
        highlighted = pygments.highlight(
            "\n".join(context), CppLexer(), TerminalFormatter()
        )
        for i, line in enumerate(highlighted.splitlines(), start=first):
            pointer = "\033[1;32m-->\033[0m" if i == line_number else "   "
            banner += f"{pointer} {i:>{width}}: {line}\n"
        return banner

    def display_surrounding_code(self) -> None:
        print_formatted_text(
            ANSI(self.get_code_context(self.file_path, self.line_number))
        )

    def _send(self, text: str) -> dict[str, object]:
        try:
            self.output.sendall(json.dumps({"input": text}).encode() + b"\0")
            return json.loads(next(self.input))  # type: ignore[no-any-return]
        except (BrokenPipeError, ConnectionResetError, StopIteration) as e:
            raise ProgramGone from e

    def _show(self, reply: dict[str, object]) -> None:
        value = str(reply.get("value", "")).rstrip("\n")
        if not value:
            return
        if reply.get("error"):
            print_formatted_text(ANSI(f"\033[31m{value}\033[0m"))
        else:
            print_formatted_text(ANSI(value))

    def show_locals(self) -> None:
        if not self.locals:
            print("no local variables captured")
        for type_, name in self.locals:
            print_formatted_text(
                HTML("  <b>{}</b> <ansigray>{}</ansigray>").format(name, type_)
            )

    def run(self) -> None:
        print("starting interpreter...", end="\r", flush=True)
        try:
            self._show(json.loads(next(self.input)))
        except StopIteration:
            print("program disconnected during startup")
            return
        try:
            completer = ClangCompleter(Path(self.file_path), self.line_number)
        except OSError:
            completer = None
        session: PromptSession[str] = PromptSession(
            history=history_file(),
            lexer=PygmentsLexer(CppLexer),
            completer=completer,
            key_bindings=_key_bindings(),
            multiline=True,
            prompt_continuation=lambda width, *_: "." * (width - 1) + " ",
            bottom_toolbar=HTML(
                f" <b>{Path(self.file_path).name}:{self.line_number}</b>"
                "  .help  .locals  .continue (Ctrl-D)"
            ),
        )
        print("Type .help for commands.")
        try:
            while True:
                try:
                    answer = session.prompt(self._prompt_string())
                except KeyboardInterrupt:
                    continue
                except EOFError:
                    answer = ".continue"
                cmd = answer.strip()
                if cmd == ".help":
                    print(HELP)
                elif cmd == ".locals":
                    self.show_locals()
                elif cmd == ".where":
                    self.display_surrounding_code()
                elif cmd in (".continue", ".quit", ".c", ".q"):
                    self._send(".quit")
                    print("continuing program.\n")
                    return
                elif cmd == ".abort":
                    self._send(".abort")
                    print("program aborted.\n")
                    return
                elif cmd:
                    self._show(self._send(answer))
        except ProgramGone:
            print("program disconnected.\n")

    def _prompt_string(self) -> str:
        prompt = f"[{self.statement_count}] "
        self.statement_count += 1
        return prompt
