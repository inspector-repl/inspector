#!/usr/bin/env python

import json
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
            response = json.dumps(dict(input=answer),
                                  ensure_ascii=True)
            self.output.sendall(response.encode("utf-8"))
            self.output.sendall(b'\0')
            response = next(self.input)
            evaluation_result = json.loads(response)
            print(evaluation_result["value"])
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
