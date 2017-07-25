
from .prebuild import INSPECTOR_MACRO
from .config import LIBRARY_PATH, INCLUDE_PATH


def print_cflags(args):
    data = dict(macro=INSPECTOR_MACRO,
                library_path=LIBRARY_PATH,
                include_path=INCLUDE_PATH)

    print("-I {include_path} -I .inspector-includes \'{macro}\' -Wl,-rpath,{library_path} -L{library_path} -linspector".format(**data))
