#!/usr/bin/env python

from __future__ import unicode_literals
from prompt_toolkit import prompt as prompt_tk
from prompt_toolkit.history import InMemoryHistory
from pygments.lexers import CppLexer

class Repl():
	def __init__(self, file_path, line_number, cling_context):
		self.file_path = file_path
		self.line_number = line_number
		self.cling_context = cling_context
		self.statement_count = 0

	def display_surrounding_code(self):
		print('Stopped at %s:%s' % (self.file_path, self.line_number))
	
	def run_interpreter_loop(self):
		history = InMemoryHistory()
		while True:
			prompt = self._prompt_string()
			answer = prompt_tk(prompt, history=history, lexer=CppLexer)
			print('You said: %s' % answer)
		print('Good bye!')

	def _prompt_string(self):
		prompt = '[%d] ' % self.statement_count
		self.statement_count += 1
		return prompt
