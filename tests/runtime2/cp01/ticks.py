#!/usr/bin/env python3
"""Sequenced edits for the CP01 modified-mode scenarios (flakiness fix, 2026-11-08).

usage: ticks.py [--age DIR]... FILE...
  sets every FILE's mtime to T - 1 ns, where T is the filesystem clock now (the
  mtime of a probe file written beside the first FILE), and prints T. With
  --age DIR, every file under DIR is first moved AGE_NS (10 s) into the past,
  all by the same amount, so their order (and every page-info's relation to its
  dependencies) is unchanged, and the edited FILEs are then strictly newer than
  everything the previous build wrote, without waiting for the clock.

The v4.12 modified-mode rule rebuilds a page when a dependency is not older than
its page-info. An edit followed at once by a build can share the filesystem's
coarse timestamp tick with the page-info that build writes (with multigrain
timestamps the two mtimes are then equal), so the page is rebuilt once more by
the following build: correct, conservative, and not what an ordinary-edit
scenario means to test. After T - 1 every later write, including the build's
page-info, is stamped at least T, because the clock never goes backwards, so
the edit is strictly older than the build whatever the scheduling.
"""
import os
import sys


def clock_now(directory):
    probe = os.path.join(directory, ".cp01-clock-probe")
    with open(probe, "w") as f:
        f.write("t")
    now = os.stat(probe).st_mtime_ns
    os.remove(probe)
    return now


AGE_NS = 10 * 10**9


def age(directory):
    for dp, dn, fs in os.walk(directory):
        for name in fs + dn:
            p = os.path.join(dp, name)
            st = os.lstat(p)
            if not os.path.islink(p):
                os.utime(p, ns=(st.st_atime_ns - AGE_NS, st.st_mtime_ns - AGE_NS))


def sequence(paths):
    now = clock_now(os.path.dirname(os.path.abspath(paths[0])))
    for p in paths:
        os.utime(p, ns=(os.stat(p).st_atime_ns, now - 1))
    return now


if __name__ == "__main__":
    args, dirs = sys.argv[1:], []
    while len(args) >= 2 and args[0] == "--age":
        dirs.append(args[1])
        args = args[2:]
    if not args:
        sys.exit("usage: ticks.py [--age DIR]... FILE...")
    for d in dirs:
        age(d)
    print(sequence(args))
