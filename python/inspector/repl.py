#!/usr/bin/env python

import json
from prompt_toolkit import prompt as prompt_tk
from prompt_toolkit.history import InMemoryHistory
import pygments
from pygments.lexers import CppLexer
from pygments.formatters import TerminalFormatter
from .clang_completer import ClangCompleter


class Repl():
    def __init__(self, input, output, file_path, line_number):
        self.input = input
        self.output = output
        self.file_path = file_path
        self.line_number = line_number
        self.statement_count = 0

    def highlight(self, lines):
        tokens = CppLexer().get_tokens("\n".join(lines))
        source = pygments.format(tokens, TerminalFormatter())
        return source.split("\n")

    def get_code_context(self, filename, line_number):
        before = max(line_number - 6, 0)
        after = line_number + 4
        context = []
        try:
            f = open(filename)

            for i, line in enumerate(f):
                if i >= before:
                    context.append(line.rstrip())
                if i > after:
                    break
            f.close()
        except IOError:
            pass
        banner = "From: {} @ line {} :\n".format(filename, line_number)
        if len(context) == 0:
            return banner

        i = max(line_number - 5, 0)

        context = self.highlight(context)

        for line in context:
            i += 1
            pointer = "-->" if i == line_number else "   "
            banner += "{} {}: {}\n".format(pointer, i, line)
        return banner

    def display_surrounding_code(self):
        banner = self.get_code_context(self.file_path, self.line_number)
        print(banner)

    def run(self):
        history = InMemoryHistory()
        try:
            completer = ClangCompleter(self.file_path, self.line_number)
        except IOError:
            completer = None
        while True:
            prompt = self._prompt_string()
            answer = prompt_tk(
                prompt, history=history, lexer=CppLexer, completer=completer)
            response = json.dumps(dict(input=answer), ensure_ascii=True)
            self.output.sendall(response.encode("utf-8"))
            self.output.sendall(b'\0')
            if answer == '.quit':
                print("Session ended.\n")
                break
            response = next(self.input)
            evaluation_result = json.loads(response)
            print(evaluation_result["value"])

    def _prompt_string(self):
        prompt = '[%d] ' % self.statement_count
        self.statement_count += 1
        return prompt


if __name__ == '__main__':
    repl = Repl("", 0, 0)
    repl.display_surrounding_code()
    repl.run()
