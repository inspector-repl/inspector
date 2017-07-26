#!/usr/bin/env python

from jsonrpcserver import methods, config
from jsonrpcserver.exceptions import InvalidParams
from repl import Repl

class PromptServer():
	def start(self):
		self.config()
		methods.serve_forever(name='', port=5000)

	def config(self):
		config.log_requests = False
		config.log_responses = False

@methods.add
def prompt(**kwargs):
	(file_path, line_number, cling_context) = validate_arguments(kwargs)
	repl = Repl(file_path, line_number, cling_context)
	repl.display_surrounding_code()
	return repl.run_interpreter_loop()


def validate_arguments(kwargs):
	kw = kwargs

	check('path', kw)
	check('lineNumber', kw)
	check('clingContext', kw)

	return (kw['path'], kw['lineNumber'], kw['clingContext'])

def check(arg, kwargs):
	if arg not in kwargs:
		raise InvalidParams('%s is required' % arg)
