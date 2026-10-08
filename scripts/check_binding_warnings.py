#!/usr/bin/env python3
"""Compile maintained native wrappers with strict warnings on available compilers."""
import os
from pathlib import Path
import shutil
import subprocess
import sysconfig

root = Path(__file__).resolve().parent.parent
node = os.environ.get('NIFT_NODE_INCLUDE')
if not node:
    candidates = [Path('/usr/include/node'), Path('/usr/local/include/node')]
    executable = shutil.which('node')
    if executable:
        candidates.append(Path(executable).resolve().parent.parent / 'include/node')
    node = next((str(p) for p in candidates if (p / 'node_api.h').is_file()), None)
if not node:
    raise SystemExit('Node headers required; set NIFT_NODE_INCLUDE')
compilers = [c for c in ('g++', 'clang++') if shutil.which(c)]
if not compilers:
    raise SystemExit('GCC or Clang required')
for compiler in compilers:
    for source, include in [('bindings/node/native/nift_node.cc', node),
                            ('bindings/python/src/nift_module.cc', sysconfig.get_path('include'))]:
        command = [compiler, '-std=c++17', '-Wall', '-Wextra', '-Wpedantic',
                        '-Werror', '-fsyntax-only', '-I' + include, '-Iinclude', source]
        subprocess.run(command, cwd=root, check=True)
        print(f'PASS {compiler}: {source} warnings as errors')
