#!/usr/bin/env python3
"""Link the actual CLI object set with an allocation-forbidden drain witness.

B10 (combined-b3-b5.md section 73): the compiler, flags, libffi archive and object
list come from the Makefile (tests/runtime2/build_toolchain.py), and a failed link prints its stderr."""
import argparse,hashlib,subprocess
from pathlib import Path
import sys as _sys,os as _os
_sys.path.insert(0,_os.path.join(_os.path.dirname(_os.path.abspath(__file__)),'..'))
import build_toolchain as toolchain
SORT_OBJECT = 'src/rt/vm_collection_sort.o'


def check_drain(out, *, detect_skip=False):
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    objects = toolchain.cli_objects(without_main=True)
    if SORT_OBJECT not in objects:
        raise RuntimeError(f'{SORT_OBJECT} is not in the CLI object list')
    driver = out / 'drain-allocation-driver.o'
    toolchain.run(toolchain.test_compile('-c', 'tests/runtime2/cp14b/drain_allocation.cpp', '-o', str(driver)),
                  'drain driver compile')

    def build(name, *flags):
        obj = out / (name + '.o')
        binary = out / name
        toolchain.run(toolchain.production_compile('-DNIFT_COLLECTION_ALLOC_TEST', *flags,
                      '-c', 'src/rt/vm_collection_sort.cpp', '-o', str(obj)), f'{name} object compile')
        toolchain.run(toolchain.link([driver, obj, *[o for o in objects if o != SORT_OBJECT]], binary),
                      f'{name} link')
        return binary

    normal = build('drain-allocation')
    if not detect_skip:
        subprocess.run([str(normal)], check=True, timeout=30)
        return

    def observe(binary, *, check=False):
        result = subprocess.run([str(binary)], check=check, capture_output=True, text=True, timeout=30)
        return dict(sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                    exit=result.returncode, stdout=result.stdout, stderr=result.stderr)

    control = observe(normal, check=True)
    mutant = observe(build('drain-skip-mutant', '-DNIFT_COLLECTION_MUTANT=9'))
    detected = (mutant['exit'] == 1 and
                mutant['stderr'] == 'FAIL caught hook performed no sort drain steps\n')
    return dict(detected=detected, control=control, mutant=mutant)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    out = args.out.resolve()
    check_drain(out)

    driver = out / 'allocation-failure-driver.o'
    toolchain.run(toolchain.test_compile('-c', 'tests/runtime2/cp14b/allocation_failure.cpp', '-o', str(driver)),
                  'allocation-failure driver compile')
    oom = out / 'allocation-failure'
    toolchain.run(toolchain.link([driver, *toolchain.cli_objects(without_main=True)], oom),
                  'allocation-failure link')
    subprocess.run([str(oom)], check=True, timeout=60)


if __name__ == '__main__':
    main()
