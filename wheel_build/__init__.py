# Copyright (C) 2026 Silimate Inc.
#
# Written by Mohamed Gaber <me@donn.website>
#
# Adapted from Yosys
#
# Copyright (C) 2026 Catherine <whitequark@whitequark.org>
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
import datetime
import os
import pathlib
import tarfile
import tempfile
import sysconfig
import subprocess
from email.policy import EmailPolicy
from email.message import EmailMessage
from typing import Tuple, Iterable, Optional
from wheel.wheelfile import WheelFile

PROJECT_NAME = "vcd2fst"
PROJECT_VERSION = os.getenv(
    "VCD2FST_WHEEL_VERSION", datetime.datetime.now().strftime("%Y.%m.%d")
)
DIST_NAME = f"{PROJECT_NAME}-{PROJECT_VERSION}"

PLATFORM_TAG_RAW = sysconfig.get_platform()
PLATFORM_TAG = (
    PLATFORM_TAG_RAW.lower().replace("-", "_").replace(".", "_").replace(" ", "_")
)
COMPAT_TAG = f"py3-none-{PLATFORM_TAG}"

# python uses ENTRY_POINTS in metadata to synthesize entries in ./venv/bin
ENTRY_POINTS = f"""
[console_scripts]
vcd2fst = {PROJECT_NAME}.__main__:vcd2fst
"""


def build_sdist(sdist_dir, config_settings=None):
    sdist_filename = f"{DIST_NAME}.tar.gz"

    with tarfile.open(
        pathlib.Path(sdist_dir) / sdist_filename,
        "w:gz",
        format=tarfile.PAX_FORMAT,
    ) as sdist:

        def exclude_build(entry):
            name = entry.name.removeprefix(f"{DIST_NAME}/")
            if name in (".cache", "build", "dist", "venv"):
                return
            if os.path.basename(name) in (".git", "__pycache__"):
                return
            return entry

        sdist.add(os.getcwd(), arcname=DIST_NAME, filter=exclude_build)

    return sdist_filename


def make_message(headers: Iterable[Tuple[str, str]], payload: Optional[str] = None):
    """
    converts a set of python tuples and an optional payload in a manner
    consistent with
    https://packaging.python.org/en/latest/specifications/core-metadata/#core-metadata
    """
    msg = EmailMessage(policy=EmailPolicy(max_line_length=0))
    for name, value in headers:
        if isinstance(value, list):
            for value_part in value:
                msg[name] = value_part
        else:
            msg[name] = value
    if payload:
        msg.set_payload(payload)
    return bytes(msg)


def get_metadata_files():
    """
    (see https://packaging.python.org/en/latest/specifications/recording-installed-packages/)
    """
    with open("README.md", "rb") as readme:
        long_description = readme.read()

    return {
        "WHEEL": make_message(
            [
                ("Wheel-Version", "1.0"),
                ("Generator", "custom silimate vcd2fst build backend"),
                ("Root-Is-Purelib", "false"),
                ("Tag", [COMPAT_TAG]),
            ]
        ),
        "METADATA": make_message(
            [
                ("Metadata-Version", "2.4"),
                ("Name", PROJECT_NAME),
                ("Version", PROJECT_VERSION),
                (
                    "Summary",
                    "Convert VCD waveform file to FST waveform file",
                ),
                ("Description-Content-Type", "text/markdown"),
                ("Classifier", "Programming Language :: Python :: 3"),
                ("Requires-Python", ">=3.8"),
                ("License", "MIT"),
            ],
            long_description,
        ),
        "entry_points.txt": ENTRY_POINTS.encode("utf8"),
    }


def prepare_metadata_for_build_wheel(metadata_directory, config_settings=None):
    """
    top-level function (called by pip during wheel build)

    generates dist-info
    """
    os.mkdir(f"{metadata_directory}/{DIST_NAME}.dist-info")

    for filename, contents in get_metadata_files().items():
        with open(f"{metadata_directory}/{DIST_NAME}.dist-info/{filename}", "wb") as f:
            f.write(contents)

    return f"{DIST_NAME}.dist-info"


def build_wheel(wheel_dir, config_settings=None, metadata_directory=None):
    """
    top-level function (called by wheel build)

    builds vcd2fst and creates python version-agnostic wheel
    """
    wheel_filename = f"{DIST_NAME}-{COMPAT_TAG}.whl"

    with WheelFile(pathlib.Path(wheel_dir) / wheel_filename, "w") as wheel:
        # write metadata
        for filename, contents in get_metadata_files().items():
            wheel.writestr(f"{DIST_NAME}.dist-info/{filename}", contents)

        # get optional cmake configuration options
        cmake_options = []
        if config_settings is not None:
            if cmake_options := config_settings.get("cmake", cmake_options):
                if isinstance(cmake_options, str):
                    cmake_options = [cmake_options]

        # build in temporary directory
        with tempfile.TemporaryDirectory(f".{PROJECT_NAME}-build", "w") as d_str:
            d = pathlib.Path(d_str)

            # copy python files
            wheel.write("wheel_build/vcd2fst/__init__.py", f"{PROJECT_NAME}/__init__.py")
            wheel.write("wheel_build/vcd2fst/__main__.py", f"{PROJECT_NAME}/__main__.py")

            # configure
            subprocess.check_call(["cmake", "-B", d, "."])

            # build
            subprocess.check_call(
                [
                    "cmake",
                    "--build",
                    d,
                    f"-j{os.cpu_count()}",
                ]
            )

            # copy binary to same location as __main__.py
            wheel.write(d / "vcd2fst", f"{PROJECT_NAME}/vcd2fst")

    return wheel_filename
