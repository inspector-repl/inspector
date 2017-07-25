#!/usr/bin/env python

from promptserver import PromptServer

def init_prompt_server():
	return PromptServer()

if __name__ == '__main__':	
	prompt = init_prompt_server()
	prompt.start()
