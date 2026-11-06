#!/usr/bin/env python3
"""B5 entry item (B4b review r5, R5-L1): the behavioural result-identity backstop.

  result_identity.py --nift NIFT

The kind inference classifies what every array method's result *is* at run time
(rt/infer_mutators.h `kCollectionResults`: receiver, fresh, stored argument, element,
callback, scalar), and B4b gave receiver-returning results the receiver's sites. The
B4b check of the run-time side read the run-time sources (a regular expression), so a
receiver-returning method written another way passed unflagged. This check observes
identity instead, under shared references (the B5 production build; before B5 the
guard-off build): for every CollectionMethod and every built-in native whose result can
be an array or object, a small program computes the result and prints `same()` against
the receiver, the stored argument or the element it should (or should not) be.

It fails when:
  - a CollectionMethod (rt/collection.h, the enum) or an array/object/any-result native
    (rt/natives/builtin_list.inc) has no probe here: a new method or native must be
    classified and probed before it can pass;
  - the probe of a CollectionMethod disagrees with its kCollectionResults entry;
  - a native's observed identity is not the one recorded below;
  - a program does not run (any exit but 0, internal text).
Programs run in a temporary directory; the repository is not written.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
INTERNAL = re.compile(r"compiler defect|internal failure|unexpected operands|\x1fnift:")

# Probes of each identity class. `R` is the result, `X` the receiver, `V` the stored
# argument, `E` the element the result should be, `I` the initial value. Each program
# prints one line per check; the expected lines are listed with it.
SCALAR = re.compile(r"^(true|false|null|-?[0-9]+(\.[0-9]+)?)$")


def receiver(call):
    return ("X := [[2], [1]]\nR := %s\nprint(same(R, X))\n" % call, ["true"])


def fresh(call, setup="X := [[2], [1]]\n"):
    # a new container: not the receiver, and two evaluations are two containers
    return (setup + "R := %s\nQ := %s\nprint(same(R, X))\nprint(same(R, Q))\n" % (call, call),
            ["false", "false"])


def fresh_of_value(call, setup=""):
    # a new container built from a non-reference receiver (or none): two evaluations differ
    return (setup + "R := %s\nQ := %s\nprint(same(R, Q))\n" % (call, call), ["false"])


def refused(call):
    # not supported on the VM: refused before any effect (the identity is then not observable)
    return ("R := %s\nprint(R)\n" % call, None)


def stored(call):
    return ("X := [[2], [1]]\nV := [9]\nR := %s\nprint(same(R, V))\n" % call, ["true"])


def element(call, setup="X := [[2], [1]]\nE := X[0]\n"):
    return (setup + "R := %s\nprint(same(R, E))\n" % call, ["true"])


def scalar(call, setup="X := [[2], [1]]\n"):
    return (setup + "R := %s\nprint(R)\n" % call, [SCALAR])


COLLECTION_PROBES = {
    "Push": ("Stored", stored("X.push(V)")),
    "Pop": ("Element", element("X.pop()", "X := [[2], [1]]\nE := X[1]\n")),
    "Insert": ("Stored", stored("X.insert(0, V)")),
    "Remove": ("Element", element("X.remove(0)")),
    "Clear": ("Scalar", scalar("X.clear()")),
    "Splice": ("Fresh", fresh("X.splice(0, 0)")),
    "Reverse": ("Receiver", receiver("X.reverse()")),
    "Sort": ("Receiver", ("X := [2, 1]\nR := X.sort()\nprint(same(R, X))\n", ["true"])),
    "ComparatorSort": ("Receiver", receiver("X.sort((p, q) => p[0] < q[0])")),
    "Map": ("Fresh", fresh("X.map((e) => e)")),
    "Filter": ("Fresh", fresh("X.filter((e) => true)")),
    "Reduce": ("Callback", ("X := [[2], [1]]\nI := [0]\nR := X.reduce((a, e) => a, I)\nprint(same(R, I))\n",
                            ["true"])),
    "Any": ("Scalar", scalar("X.any((e) => true)")),
    "All": ("Scalar", scalar("X.all((e) => true)")),
    "Find": ("Element", element("X.find((e) => true)")),
    "FindIndex": ("Scalar", scalar("X.find_index((e) => true)")),
    "Count": ("Scalar", scalar("X.count((e) => true)")),
    "SortBy": ("Fresh", fresh("X.sort_by((e) => e[0])")),
    "GroupBy": ("Fresh", fresh('X.group_by((e) => "g")')),
    "IndexBy": ("Fresh", fresh("X.index_by((e) => e[0].to_string())")),
    "Partition": ("Fresh", fresh("X.partition((e) => true)")),
    "UniqueBy": ("Fresh", fresh("X.unique_by((e) => e[0])")),
    "MinBy": ("Element", element("X.min_by((e) => 0)")),
    "MaxBy": ("Element", element("X.max_by((e) => 0)")),
    "CountBy": ("Fresh", fresh('X.count_by((e) => "g")')),
    "GroupByEach": ("Fresh", fresh('X.group_by_each((e) => ["g"])')),
}

# Natives (rt/natives/builtin_list.inc) whose result kinds include array, object or any:
# id -> (identity, probe). Identity: Fresh (a new container), Element (the receiver's
# element or member, or a map/stack element), Argument (the argument it validates or the
# default it returns), Scalar (an immutable value: string, number, bool, null, bytes),
# Refused (not supported on the VM: refused before any effect).
NATIVE_PROBES = {
    "StrSplit": ("Fresh", fresh_of_value('X.split(",")', 'X := "a,b"\n')),
    "ArrFirst": ("Element", element("X.first()")),
    "ArrLast": ("Element", element("X.last()", "X := [[2], [1]]\nE := X[1]\n")),
    "ArrSlice": ("Fresh", fresh("X.slice(0)")),
    "ArrUnique": ("Fresh", fresh("X.unique()")),
    "ArrFlatten": ("Fresh", fresh("X.flatten()")),
    "ArrMin": ("Scalar", scalar("X.min()", "X := [2, 1]\n")),
    "ArrMax": ("Scalar", scalar("X.max()", "X := [2, 1]\n")),
    "ArrTake": ("Fresh", fresh("X.take(10)")),
    "ArrDrop": ("Fresh", fresh("X.drop(0)")),
    "ArrChunk": ("Fresh", fresh("X.chunk(5)")),
    "ArrFromEntries": ("Fresh", fresh("X.from_entries()", 'X := [{"key": "k", "value": [1]}]\n')),
    "FnMin": ("Scalar", scalar("min(1, 2)")),
    "FnMax": ("Scalar", scalar("max(1, 2)")),
    "FnRange": ("Fresh", fresh_of_value("range(3)")),
    "ObjKeys": ("Fresh", fresh("X.keys()", 'X := {"k": [1]}\n')),
    "ObjValues": ("Fresh", fresh("X.values()", 'X := {"k": [1]}\n')),
    "ObjEntries": ("Fresh", fresh("X.entries()", 'X := {"k": [1]}\n')),
    "ObjGet": ("Element", element('X.get("k")', 'X := {"k": [1]}\nE := X["k"]\n')),
    "ObjGet/default": ("Argument", ('X := {"k": [1]}\nV := [5]\nR := X.get("z", V)\nprint(same(R, V))\n',
                                    ["true"])),
    "ObjMerge": ("Fresh", fresh('X.merge({"j": 2})', 'X := {"k": [1]}\n')),
    "ObjMergeDeep": ("Fresh", fresh('X.merge_deep({"j": 2})', 'X := {"k": [1]}\n')),
    "ObjPick": ("Fresh", fresh('X.pick(["k"])', 'X := {"k": [1]}\n')),
    "ObjOmit": ("Fresh", fresh('X.omit(["j"])', 'X := {"k": [1]}\n')),
    "FnValidate": ("Argument", ('V := [1]\nR := validate({"type": "array"}, V)\nprint(same(R, V))\n', ["true"])),
    "FnCopy": ("Fresh", fresh("copy(X)")),
    "FnDeepcopy": ("Fresh", fresh("deepcopy(X)")),
    "HostOpenBytes": ("Refused", refused('open_bytes("b.txt")')),
    "HostStat": ("Fresh", fresh_of_value('stat(".")')),
    "HostLs": ("Fresh", fresh_of_value('ls(".")')),
    "HostEnv": ("Fresh", fresh_of_value("env()")),
    "HostSecureRandomBytes": ("Refused", refused("secure_random_bytes(2)")),
    "MapGet": ("Element", element('X.get("k")', 'X := map()\nE := [1]\nX.set("k", E)\n')),
    "StackTop": ("Element", element("X.top()", "X := stack()\nE := [1]\nX.push(E)\n")),
    "StackFront": ("Element", element("X.front()", "X := stack()\nE := [1]\nX.push(E)\n")),
}


def collection_methods():
    text = open(os.path.join(ROOT, "src/rt/collection.h")).read()
    body = re.search(r"enum class CollectionMethod[^{]*\{([^}]*)\}", text).group(1)
    names = [n.strip() for n in body.split(",") if n.strip()]
    assert names[-1] == "CountMethods", names
    return names[:-1]


def table():
    text = open(os.path.join(ROOT, "src/rt/infer_mutators.h")).read()
    body = re.search(r"kCollectionResults\[\] = \{(.*?)\};", text, re.S).group(1)
    return [m.group(1) for m in re.finditer(r"ResultIdentity::(\w+),", body)]


def natives():
    out = []
    for line in open(os.path.join(ROOT, "src/rt/natives/builtin_list.inc")):
        if not line.startswith("NIFT_RT_BUILTIN("):
            continue
        fields = [f.strip() for f in line[len("NIFT_RT_BUILTIN("):].split(",")]
        if re.search(r"\b(kAny|kArray|kObject)\b", fields[5]):
            out.append(fields[0])
    return out


def run(nift, src):
    d = tempfile.mkdtemp(prefix="b5-identity-")
    try:
        with open(os.path.join(d, "p.f"), "w") as f:
            f.write(src)
        with open(os.path.join(d, "b.txt"), "w") as f:
            f.write("ab")
        env = {"PATH": "/usr/bin:/bin", "HOME": d, "TMPDIR": d, "LANG": "C", "LC_ALL": "C", "NIFT_VM": "require"}
        p = subprocess.run(["timeout", "30", nift, "p.f"], cwd=d, env=env, stdin=subprocess.DEVNULL,
                           capture_output=True)
        return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")
    finally:
        subprocess.run(["rm", "-rf", d])


def check(nift, label, expected_class, probe, recorded, failures):
    src, want = probe
    rc, out, err = run(nift, src)
    lines = out.splitlines()
    if want is None:  # refused before any effect
        ok = rc == 1 and not out and "not compiled for the VM" in err
    else:
        ok = rc == 0 and not INTERNAL.search(out + err) and len(lines) == len(want) and all(
            (w.match(l) if hasattr(w, "match") else l == w) for w, l in zip(want, lines))
    if expected_class != recorded:
        failures.append("%s: the probe is written for %s but the table says %s" % (label, expected_class, recorded))
    elif not ok:
        failures.append("%s (%s): exit %d, output %r, stderr %r" % (label, recorded, rc, out, err.strip()[:200]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    args = ap.parse_args()
    failures = []
    methods, results = collection_methods(), table()
    if len(methods) != len(results):
        failures.append("kCollectionResults has %d entries for %d methods" % (len(results), len(methods)))
    for name, recorded in zip(methods, results):
        if name not in COLLECTION_PROBES:
            failures.append("CollectionMethod %s has no identity probe (classify and probe it)" % name)
            continue
        expected, probe = COLLECTION_PROBES[name]
        check(args.nift, name, expected, probe, recorded, failures)
    for name in sorted(set(COLLECTION_PROBES) - set(methods)):
        failures.append("probe for %s, which is not a CollectionMethod" % name)
    listed = natives()
    for name in listed:
        if name not in NATIVE_PROBES:
            failures.append("native %s returns an array, object or any and has no identity probe" % name)
    for name, (identity, probe) in sorted(NATIVE_PROBES.items()):
        if name.split("/")[0] not in listed:
            failures.append("probe for native %s, which builtin_list.inc does not list" % name)
            continue
        check(args.nift, name, identity, probe, identity, failures)
    checks = len(methods) + len(NATIVE_PROBES)
    for f in failures:
        print("FAIL " + f)
    if failures:
        print("FAIL result identity: %d of %d probes" % (len(failures), checks))
        return 1
    print("PASS result identity: %d CollectionMethods against kCollectionResults, %d native probes" %
          (len(methods), len(NATIVE_PROBES)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
