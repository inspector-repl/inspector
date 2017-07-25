
from .prebuild import INSPECTOR_MACRO


def print_cflags(args):
    data = dict(macro=INSPECTOR_MACRO,
            library_path="")
    print("{macro} -L {library_path} -linspector".format(data))
