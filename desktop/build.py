#!/usr/bin/env python3
"""Build OpenStint Desktop into a single binary using PyInstaller."""

import subprocess
import sys
import os
import glob

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD_SRC = os.path.join(ROOT, "build", "src")

so_files = glob.glob(os.path.join(BUILD_SRC, "openstint*.so")) + \
           glob.glob(os.path.join(BUILD_SRC, "openstint*.pyd"))

if not so_files:
    print(f"ERROR: No openstint shared library found in {BUILD_SRC}")
    print("Build the C++ project first: mkdir build && cd build && cmake .. && make")
    sys.exit(1)

datas = []
for f in so_files:
    datas.extend(["--add-binary", f"{f}{os.pathsep}."])

# Bundle native DLLs that the openstint module depends on
deps_dir = os.environ.get("DEPS_DIR", "")
if deps_dir:
    for dll in glob.glob(os.path.join(deps_dir, "**", "*.dll"), recursive=True):
        datas.extend(["--add-binary", f"{dll}{os.pathsep}."])

cmd = [
    sys.executable, "-m", "PyInstaller",
    "--onefile",
    "--name", "openstint-desktop",
    "--windowed",
    "--hidden-import=zmq",
    "--collect-all=pyrtlsdrlib",
    *datas,
    os.path.join(HERE, "app.py"),
]

print("Running:", " ".join(cmd))
subprocess.run(cmd, check=True)
