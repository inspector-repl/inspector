
from .config import LIBRARY_PATH

INSPECTOR_MACRO = '-DINSPECTOR=<inspector/__FILE__-__LINE__>'

def print_cflags(args):
    data = dict(macro=INSPECTOR_MACRO, library_path=LIBRARY_PATH)

    print("-I .inspector-includes {macro} -Wl,-rpath,{library_path} -L{library_path} -linspector".format(**data))
