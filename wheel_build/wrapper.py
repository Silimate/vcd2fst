import os
import sys
from pathlib import Path

__file_dir__ = Path(__file__).absolute().parent


def vcd2fst():
    binary_path = __file_dir__ / "vcd2fst"
    os.execlp(binary_path, "vcd2fst", *sys.argv[1:])
