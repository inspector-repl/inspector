import re
from subprocess import Popen, PIPE
from pathlib import Path
from typing import Iterator, Any
from prompt_toolkit.completion import Completer, Completion
from prompt_toolkit.document import Document


class ClangCompleter(Completer):
    def __init__(self, file_path: Path, line: int) -> None:
        self.file_path = file_path
        self.line = line
        self.insertion_pattern = re.compile("[:.][^:.]*")
        with open(self.file_path) as f:
            self.content = f.readlines()

    def run_clang(self, document: str) -> list[str]:
        column = len(document) + 1
        self.content[self.line - 1] = document
        cmd = [
            "clang++",
            "-fsyntax-only",
            "-Xclang",
            "-code-completion-macros",
            "-Xclang",
            "-code-completion-at=-:%d:%d" % (self.line, column),
            "-x",
            "c++",
            "-",
        ]
        p = Popen(cmd, stdin=PIPE, stdout=PIPE, stderr=PIPE)
        compilation_unit = "".join(self.content).encode("utf-8")
        stdout, stderr = p.communicate(compilation_unit)
        lines = stdout.split(b"\n")
        symbols = []
        for line in lines:
            parts = line.split(b": ")
            if len(parts) != 3 or parts[0] != b"COMPLETION":
                continue
            symbols.append(parts[1].rstrip().decode("utf-8"))
        return symbols

    def get_completions(
        self, document: Document, complete_event: Any
    ) -> Iterator[Completion]:
        text = document.current_line_before_cursor
        completions = self.run_clang(text)
        match = None
        for m in self.insertion_pattern.finditer(text):
            match = m
        if match is not None:
            prefix_start = match.start() + 1
            prefix_length = len(text) - prefix_start
            offset = -prefix_length
        else:
            offset = -len(text)
        for completion in completions:
            yield Completion(completion, start_position=offset)
