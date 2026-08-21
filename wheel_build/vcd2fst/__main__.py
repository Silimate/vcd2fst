import os
import sys

from . import VCD2FST_BIN_PATH

def vcd2fst():
    os.execlp(VCD2FST_BIN_PATH, "vcd2fst", *sys.argv[1:])

if __name__ == "__main__":
    vcd2fst()
