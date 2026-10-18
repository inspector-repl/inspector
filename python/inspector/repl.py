import json
import socket
from collections.abc import Iterator
from pathlib import Path

import pygments
from prompt_toolkit import prompt as prompt_tk
from prompt_toolkit.history import InMemoryHistory
from prompt_toolkit.lexers import PygmentsLexer
from pygments.formatters import TerminalFormatter
from pygments.lexers import CppLexer

from .clang_completer import ClangCompleter


class Repl:
    def __init__(
        self,
        input: Iterator[str],
        output: socket.socket,
        file_path: str,
        line_number: int,
    ) -> None:
        self.input = input
        self.output = output
        self.file_path = file_path
        self.line_number = line_number
        self.statement_count = 0

    def highlight(self, lines: list[str]) -> list[str]:
        tokens = CppLexer().get_tokens("\n".join(lines))
        source = pygments.format(tokens, TerminalFormatter())
        return source.split("\n")

    def get_code_context(self, filename: str, line_number: int) -> str:
        before = max(line_number - 6, 0)
        after = line_number + 4
        context = []
        try:
            with open(filename) as f:
                for i, line in enumerate(f):
                    if i >= before:
                        context.append(line.rstrip())
                    if i > after:
                        break
        except OSError:
            pass
        banner = f"From: {filename} @ line {line_number} :\n"
        if len(context) == 0:
            return banner

        i = max(line_number - 5, 0)

        context = self.highlight(context)

        for line in context:
            i += 1
            pointer = "-->" if i == line_number else "   "
            banner += f"{pointer} {i}: {line}\n"
        return banner

    def display_surrounding_code(self) -> None:
        banner = self.get_code_context(self.file_path, self.line_number)
        print(banner)

    def run(self) -> None:
        history = InMemoryHistory()
        try:
            completer = ClangCompleter(Path(self.file_path), self.line_number)
        except OSError:
            completer = None
        while True:
            prompt = self._prompt_string()
            answer = prompt_tk(
                prompt,
                history=history,
                lexer=PygmentsLexer(CppLexer),
                completer=completer,
            )
            response = json.dumps({"input": answer}, ensure_ascii=True)
            try:
                self.output.sendall(response.encode("utf-8"))
                self.output.sendall(b"\0")
            except BrokenPipeError:
                # Client disconnected, exit gracefully
                print("Client disconnected.\n")
                break
            if answer == ".quit":
                print("Session ended.\n")
                break
            response = next(self.input)
            evaluation_result = json.loads(response)
            print(evaluation_result["value"])

    def _prompt_string(self) -> str:
        prompt = f"[{self.statement_count}] "
        self.statement_count += 1
        return prompt
