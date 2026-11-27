#!/usr/bin/env python3
"""The permanent effect classification of every built-in (slot 2, owner 2026-11-11 05:41, D-068 #2; review condition C3).

src/rt/module_host_order.cpp's builtin_effect() classifies each natives::BuiltinId as Pure, Host or Shared;
the host-import guard admits only Pure natives (and print) while modules initialise, and refuses an
Unclassified one. This check enumerates every BuiltinId of src/rt/natives/builtin_list.inc (it does not
filter a name list) and fails when:

  - an id is not classified, or classified twice, or the classification names an id that does not exist;
  - the Host class differs from the native host predicate (the R1 block and appended stream natives);
  - a built-in that touches the file system, the environment, processes, the clock or console input
    (EFFECTFUL below: names, and every File and Stream method) is not Host;
  - a Shared built-in (synchronisation, channels) is classified Pure.

--mutant appends an unclassified built-in to a scratch copy of builtin_list.inc and runs the same check
on it: the check must then fail and name it (negative control).

usage: host_set_complete.py [--mutant]
"""
import argparse
import os
import re
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LIST = os.path.join(ROOT, "src", "rt", "natives", "builtin_list.inc")
HEADER = os.path.join(ROOT, "src", "rt", "natives", "builtin.h")
CLASSIFICATION = os.path.join(ROOT, "src", "rt", "module_host_order.cpp")
EFFECTFUL = {"open", "open_bytes", "cat", "sha256_file", "exists", "is_file", "is_dir", "stat", "ls", "pwd", "cd",
             "touch", "mkdir", "make_dir", "remove", "rm", "cp", "mv", "which", "getenv", "env", "setenv",
             "unsetenv", "os", "arch", "platform", "hardware_concurrency", "epoch", "sleep", "secure_random_bytes",
             "file", "ifstream", "ofstream", "read", "read_all", "read_line", "write", "write_line", "run", "cmd"}
MUTANT_LINE = 'NIFT_RT_BUILTIN(FnUnclassifiedProbe, "unclassified_probe", None, 0, 0, kNull, Fixed, false, 60)\n'


def builtins(path):
    rows = re.findall(r'^NIFT_RT_BUILTIN\((\w+), "([^"]*)", (\w+),', open(path).read(), re.M)
    return [(i, name, receiver) for i, name, receiver in rows]


def classification(path):
    text = open(path).read()
    body = text[text.index("BuiltinEffect builtin_effect("):]
    body = body[:body.index("\n}\n")]
    classes, pending = {}, []
    for line in body.splitlines():
        pending += re.findall(r"case I::(\w+):", line)
        m = re.search(r"return BuiltinEffect::(\w+);", line)
        if m:
            for i in pending:
                classes.setdefault(i, []).append(m.group(1))
            pending = []
    return classes


def check(list_path):
    problems = []
    ids = builtins(list_path)
    classes = classification(CLASSIFICATION)
    names = [i for i, _, _ in ids]
    for i, name, receiver in ids:
        got = classes.get(i, [])
        if not got:
            problems.append("unclassified built-in: %s (%s)" % (i, name))
        elif len(got) > 1:
            problems.append("classified more than once: %s %s" % (i, got))
    for i in classes:
        if i not in names and i != "Count":
            problems.append("classification names an unknown id: %s" % i)
    classifier = open(HEADER).read().split("bool host_builtin(BuiltinId id)", 1)[1].split("}", 1)[0]
    block = set(re.findall(r"id == BuiltinId::(\w+)", classifier))
    for first, last in re.findall(r"id >= BuiltinId::(\w+) && id <= BuiltinId::(\w+)", classifier):
        block.update(names[names.index(first):names.index(last) + 1])
    host = {i for i, c in classes.items() if c == ["Host"]}
    if host != block:
        problems.append("Host class differs from natives::host_builtin(): extra %s, missing %s" %
                        (sorted(host - block), sorted(block - host)))
    for i, name, receiver in ids:
        c = classes.get(i, [])
        if (name in EFFECTFUL or receiver in ("File", "Stream")) and c and c != ["Host"]:
            problems.append("effectful built-in not classified Host: %s (%s, %s) is %s" % (i, name, receiver, c))
        if i.startswith(("Shared", "Mutex", "Atomic")) and c == ["Pure"]:
            problems.append("synchronisation built-in classified Pure: %s" % i)
    return problems, len(ids)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mutant", action="store_true")
    a = ap.parse_args()
    if not a.mutant:
        problems, n = check(LIST)
        for p in problems:
            print("FAIL host set: " + p)
        print("%s host set: %d built-ins, each classified Pure, Host or Shared; Host matches the native predicate" %
              ("FAIL" if problems else "PASS", n))
        return 1 if problems else 0
    with tempfile.TemporaryDirectory(prefix="host-set-") as d:
        copy = os.path.join(d, "builtin_list.inc")
        shutil.copy(LIST, copy)
        with open(copy, "a") as f:
            f.write(MUTANT_LINE)
        problems, _ = check(copy)
    if any("unclassified built-in: FnUnclassifiedProbe" in p for p in problems):
        print("PASS host set mutant: an appended unclassified built-in is caught (%s)" % problems[0])
        return 0
    print("FAIL host set mutant: an unclassified built-in was not caught (%s)" % (problems or "no problems"))
    return 1


if __name__ == "__main__":
    sys.exit(main())
