#!/usr/bin/env python3
"""The toolchain of the harnesses that compile or link, taken from the Makefile (B10, B15;
combined-b3-b5.md sections 73 and 75).

The cp14b and cp23 harnesses used to call `g++` by name, link a hard-coded libffi path
(.build/libffi/x86_64-linux-gnu-cc/...) and collect the objects to link with
rglob('*.o'), which on a GCC 15 build picked the wrong compiler and a libffi
directory that does not exist, and missed or added objects (PackageTransaction).
The Makefile now passes everything through the environment (HARNESS_TOOLCHAIN_ENV):

  NIFT_TC_CXX           $(CXX)
  NIFT_TC_GUARD         $(PRODUCTION_GUARD), for production translation units only
  NIFT_TC_CPPFLAGS      $(CPPFLAGS)
  NIFT_TC_CXXFLAGS      $(CXXFLAGS)
  NIFT_TC_LDFLAGS       $(LDFLAGS)
  NIFT_TC_LIBFFI_A      $(LIBFFI_A)
  NIFT_TC_CLI_OBJECTS   $(CLI_OBJECTS), the objects nift itself links
  NIFT_TC_RT_SOURCES    $(RT_VALUE_SOURCES) src/RuntimeValue.cpp, the RT unit set

A harness run outside make stops with a message naming the missing variable.
Every compile and link goes through run(), which prints the tool's stderr on failure.
"""
import os
import shlex
import subprocess
import sys


def _need(name, may_be_empty=False):
    value = os.environ.get(name)
    if value is None or (not value.strip() and not may_be_empty):
        sys.exit(f"harness toolchain: {name} is not set; run this harness through make (B10)")
    return value


def words(name, may_be_empty=False):
    """A Makefile value from the environment, split as the shell would."""
    return shlex.split(_need(name, may_be_empty))


def cxx():
    return words("NIFT_TC_CXX")


def guard():
    return words("NIFT_TC_GUARD")


def cppflags():
    return words("NIFT_TC_CPPFLAGS")


def cxxflags():
    return words("NIFT_TC_CXXFLAGS")


def ldflags():
    return words("NIFT_TC_LDFLAGS", may_be_empty=True)


def libffi_a():
    path = _need("NIFT_TC_LIBFFI_A").strip()
    if not os.path.isfile(path):
        sys.exit(f"harness toolchain: LIBFFI_A {path} does not exist; build nift first (B10)")
    return path


def cli_objects(without_main=False):
    objects = words("NIFT_TC_CLI_OBJECTS")
    return [o for o in objects if not (without_main and o == "src/nift.o")]


def rt_sources():
    return words("NIFT_TC_RT_SOURCES")


def production_compile(*extra):
    """Compile flags for a production translation unit (or a mutant of one): exactly nift's."""
    return [*cxx(), *guard(), *cppflags(), *cxxflags(), *extra]


def test_compile(*extra):
    """Compile flags for a test translation unit (tests/...): no production guard."""
    return [*cxx(), *cppflags(), "-Itests/rt", *cxxflags(), *extra]


def link(objects, output, *extra):
    """Link like nift: $(CXX) $(CXXFLAGS) $(LDFLAGS) objects $(LDLIBS)."""
    return [*cxx(), *cxxflags(), *ldflags(), *extra, *map(str, objects), "-ldl", libffi_a(), "-o", str(output)]


def run(command, what, log=None):
    """Run a compile or link; on failure print its stderr and stop."""
    proc = subprocess.run(list(map(str, command)), capture_output=True, text=True)
    if log is not None:
        with open(log, "w") as handle:
            handle.write(proc.stdout + proc.stderr)
    if proc.returncode:
        sys.stderr.write(f"harness toolchain: {what} failed (exit {proc.returncode}): {shlex.join(map(str, command))[:2000]}\n")
        sys.stderr.write(proc.stdout[-4000:] + proc.stderr[-12000:])
        raise RuntimeError(f"{what} failed (exit {proc.returncode})")
    return proc


def version():
    return subprocess.check_output([*cxx(), "--version"], text=True)
