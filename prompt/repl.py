#!/usr/bin/env python

from __future__ import unicode_literals
from prompt_toolkit import prompt as prompt_tk
from prompt_toolkit.history import InMemoryHistory
from pygments.lexers import CppLexer

class Repl():
    def __init__(self, input, output, file_path, line_number):
       self.input = input
       self.output = output
       self.file_path = file_path
       self.line_number = line_number
       self.statement_count = 0

    def display_surrounding_code(self):
        print('Stopped at %s:%s' % (self.file_path, self.line_number))

    def run_interpreter_loop(self):
        history = InMemoryHistory()
        while True:
            prompt = self._prompt_string()
            answer = prompt_tk(prompt, history=history, lexer=CppLexer)
            if answer == '.quit':
                break
            self.output.write(answer)
            print(self.input.readline())
        return _to_string(history)

    def _prompt_string(self):
        prompt = '[%d] ' % self.statement_count
        self.statement_count += 1
        return prompt

def _diplay(history):
    print('history:', end=' ')
    for line in history[:-1]:
        print(line, end=' ')
    print('', flush=True)

def _to_string(history):
    output = ''
    for line in history[:-1]:
        output += ' ' + line
    return output

if __name__ == '__main__':
    repl = Repl("", 0, 0)
    repl.display_surrounding_code()
    repl.run_interpreter_loop()
