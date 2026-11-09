#!/usr/bin/env python3
"""B09 (docs/runtime-2/checkpoints/combined-b3-b5.md section 51): the production objects a test
binary needs, derived from the production object list instead of a hand-written source list.

Starting from the seed objects (the test's own subject: for the isolated built-in tests, the
natives other than the Host handle natives, plus the stubs object), every undefined symbol is
resolved against the candidate production objects (the production list minus --exclude), adding
the defining object until nothing more is needed. A symbol that only an excluded object defines
is the isolation contract breaking (for example a native that needs the legacy value bridge):
the script fails and names it, instead of a stale list failing to link.

Prints the objects to link, one per line (seeds first). Unresolved symbols that no object defines
are left to the linker (the C++ runtime, libc).

usage: link_closure.py --seed OBJ... --candidates OBJ... [--exclude OBJ...]
"""
import argparse
import subprocess
import sys


def symbols(obj):
    out = subprocess.run(["nm", "-g", obj], capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit("link_closure: nm %s failed: %s" % (obj, out.stderr.strip()))
    defined, undefined = set(), set()
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] in ("U", "w", "v"):
            if parts[0] == "U":
                undefined.add(parts[1])
        elif len(parts) == 3 and parts[1] not in ("U", "w", "v"):
            defined.add(parts[2])
    return defined, undefined


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", nargs="+", required=True)
    ap.add_argument("--candidates", nargs="+", required=True)
    ap.add_argument("--exclude", nargs="*", default=[])
    a = ap.parse_args()
    excluded = set(a.exclude)
    table = {}
    for obj in dict.fromkeys(a.candidates + a.exclude):
        table[obj] = symbols(obj)
    providers = {}
    for obj, (defined, _) in table.items():
        for s in defined:
            providers.setdefault(s, []).append(obj)
    chosen, defined, needed_by, why = [], set(), {}, {}
    for obj in a.seed:
        d, u = table[obj] if obj in table else symbols(obj)
        chosen.append(obj)
        defined |= d
        for s in u:
            needed_by.setdefault(s, obj)
    forbidden = {}
    queue = sorted(set(needed_by) - defined)
    while queue:
        s = queue.pop()
        if s in defined:
            continue
        objs = providers.get(s, [])
        if not objs:
            continue  # the runtime libraries
        usable = [o for o in objs if o not in excluded]
        if not usable:
            forbidden.setdefault(s, needed_by[s])
            continue
        obj = usable[0]
        if obj in chosen:
            continue
        chosen.append(obj)
        why[obj] = (s, needed_by[s])
        d, u = table[obj]
        defined |= d
        for t in sorted(u - defined):
            if t not in needed_by:
                needed_by[t] = obj
                queue.append(t)
    if forbidden:
        for s, user in sorted(forbidden.items(), key=lambda kv: kv[1]):
            chain = [user]
            while chain[-1] in why:
                chain.append(why[chain[-1]][1])
            print("link_closure: %s (needed by %s) is defined only in an excluded object"
                  % (s, " <- ".join(chain)), file=sys.stderr)
        sys.exit("link_closure: the isolation contract is broken (%d symbols)" % len(forbidden))
    print("\n".join(chosen))
    return 0


if __name__ == "__main__":
    sys.exit(main())
