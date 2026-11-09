#!/usr/bin/env python3
"""B03 (combined-b3-b5.md section 48): no thread_local with a destructor in nift.

A thread_local object with a non-trivial destructor is registered with __cxa_thread_atexit on its
first use; glibc (2.39+) aborts the process when that registration cannot be allocated ("Fatal
glibc error: failed to register TLS destructor: out of memory", SIGABRT), so a first use after
memory ran out (the VM's teardown after an out-of-memory stop: test-rt-memory-c1) turned an
ordinary memory error into an abort. Per-thread objects use nift::tls::owned<T>()
(src/rt/thread_owned.h) instead.

Deterministic check: the binary imports no __cxa_thread_atexit, and (with --objects) no object
references it; a failure names the objects and their TLS symbols.

usage: tls_destructors.py --nift NIFT [--objects OBJ ...]
"""
import argparse
import subprocess
import sys


def references(path, dynamic=False):
    out = subprocess.run(["nm", "-D" if dynamic else "-u", path], capture_output=True, text=True).stdout
    return any("__cxa_thread_atexit" in line for line in out.splitlines())


def tls_symbols(path):
    out = subprocess.run(["readelf", "-sW", path], capture_output=True, text=True).stdout
    names = [f[7] for f in (l.split() for l in out.splitlines()) if len(f) > 7 and f[3] == "TLS"]
    demangled = subprocess.run(["c++filt"], input="\n".join(names), capture_output=True, text=True).stdout
    return [n for n in demangled.splitlines() if n]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--objects", nargs="*", default=[])
    a = ap.parse_args()
    fails = 0
    if references(a.nift, dynamic=True):
        fails += 1
        print("FAIL tls destructors: %s imports __cxa_thread_atexit (a thread_local with a destructor)" % a.nift)
    else:
        print("PASS tls destructors: %s imports no __cxa_thread_atexit" % a.nift)
    bad = [o for o in a.objects if o.endswith(".o") and references(o)]
    for o in bad:
        fails += 1
        print("FAIL tls destructors: %s registers a TLS destructor; TLS symbols: %s" % (o, ", ".join(tls_symbols(o))))
    if a.objects:
        print(("FAIL" if bad else "PASS") + " tls destructors: %d objects, %d registering" % (len(a.objects), len(bad)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
