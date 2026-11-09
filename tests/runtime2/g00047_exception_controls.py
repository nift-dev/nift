#!/usr/bin/env python3
"""The g00047 one-row exception (owner 2026-11-10 10:10; combined-b3-b5.md section 70).

The mutation gate's parity corpus (rt-diff --gen 50 --seed 3) contains gen/g00047/main.f, whose
only difference is the B5 shared-reference one (`[1,2]` on legacy, `[1,2,99]` on the VM). The
sharing oracle leaves it UNCLASSIFIED (thread(); its fail-closed rules are unchanged); two exact
rules in tools/rt-diff/policies/b5-refsem-vm.json approve exactly that line, on stdout and the
combined channel, pinned to seed 3, main.f's sha256 and the program directory's sha256.

This runs rt-diff on g00047 alone (generated afresh with gen.py --seed 3), with only those two
rules loaded (copied verbatim from the policy file), and requires:
  positive  the real VM: approved by both rules, nothing differing, the oracle still UNCLASSIFIED;
  (a)       an unrelated extra stdout line besides the difference: rejected;
  (b)       an extra stderr line, or a changed exit status: rejected;
  (c)       a one-byte source change that keeps the difference: the rules stop matching (expiry);
  (d)       an extra file in the program's directory (a module input): the rules stop matching;
  (e)       the same rules under --seed 1: out of scope (neither used nor unused).

usage: g00047_exception_controls.py --nift NIFT [--parse-dump .build/rt-parse-dump]
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
RULES = ("b5-gen-seed3_g00047_main-stdout", "b5-gen-seed3_g00047_main-combined")


def wrapper(path, body):
    with open(path, "w") as handle:
        handle.write("#!/bin/sh\n" + body + "\n")
    os.chmod(path, 0o755)
    return path


def rt_diff(a, b, corpus, policy, dump, report, seed=3):
    cmd = [sys.executable, "-B", os.path.join(REPO, "tools", "rt-diff", "rt-diff"), "--a", a, "--b", b,
           "--gen", "1", "--seed", str(seed), "--corpus", "gen=" + corpus, "--policy", policy, "--quiet",
           "--b-vm-stats", "--sharing-oracle", dump, "--report", report, "--timeout", "60", "--jobs", "2"]
    p = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True)
    if p.returncode not in (0, 1) or not os.path.exists(report):
        sys.exit("FAIL rt-diff harness error (exit %d): %s" % (p.returncode, p.stderr[-2000:]))
    with open(report) as handle:
        rep = json.load(handle)
    entry = next(r for r in rep["results"] if r["id"] == "gen/g00047/main.f")
    return p.returncode, rep, entry


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--parse-dump", default=os.environ.get("VM_PARSE_DUMP", os.path.join(REPO, ".build", "rt-parse-dump")))
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    with open(os.path.join(REPO, "tools", "rt-diff", "policies", "b5-refsem-vm.json")) as handle:
        rules = [r for r in json.load(handle) if r["id"] in RULES]
    if sorted(r["id"] for r in rules) != sorted(RULES):
        sys.exit("FAIL the two g00047 rules are not in b5-refsem-vm.json")
    if any(r["channel"] not in ("stdout", "combined") for r in rules):
        sys.exit("FAIL a g00047 rule covers a channel other than stdout/combined")
    failures = 0
    work = tempfile.mkdtemp(prefix="g00047-controls-")
    try:
        policy = os.path.join(work, "g00047.json")
        with open(policy, "w") as handle:
            json.dump(rules, handle)
        subprocess.run([sys.executable, "-B", os.path.join(REPO, "tools", "rt-diff", "gen.py"), "--count", "48",
                        "--seed", "3", "--out", os.path.join(work, "gen")], check=True, stdout=subprocess.DEVNULL)
        legacy = wrapper(os.path.join(work, "legacy"), "NIFT_VM=0\nexport NIFT_VM\nexec '%s' \"$@\"" % nift)
        vm = wrapper(os.path.join(work, "vm"), "NIFT_VM=1\nexport NIFT_VM\nexec '%s' \"$@\"" % nift)
        fake = lambda mode: wrapper(os.path.join(work, "fake-" + mode), "FAKE_VM_MODE=%s\nexport FAKE_VM_MODE\nexec '%s' -B '%s' '%s' \"$@\"" % (  # noqa: E731
            mode, sys.executable, os.path.join(HERE, "sharing_oracle_fake_vm.py"), nift))

        def corpus(name, edit=None):
            root = os.path.join(work, "c-" + name)
            shutil.copytree(os.path.join(work, "gen", "g00047"), os.path.join(root, "g00047"))
            if edit:
                edit(os.path.join(root, "g00047"))
            return root

        def report(name, ok, detail):
            nonlocal failures
            print("%s %-40s %s" % ("PASS" if ok else "FAIL", name, detail))
            failures += 0 if ok else 1

        rc, rep, e = rt_diff(legacy, vm, corpus("real"), policy, args.parse_dump, os.path.join(work, "real.json"))
        approved = sorted(d["policy"] for d in e["approved"])
        empty = os.path.join(work, "none.json")
        with open(empty, "w") as handle:
            handle.write("[]")
        _, _, bare = rt_diff(legacy, vm, corpus("no-rules"), empty, args.parse_dump, os.path.join(work, "none-r.json"))
        oracle = (bare.get("sharing_oracle") or {}).get("status")
        oracle_why = (bare.get("sharing_oracle") or {}).get("reason", "")
        report("positive: the exact rules approve g00047", rc == 0 and not e["diffs"] and approved == sorted(RULES)
               and not rep["summary"]["unused_policies"], "approved by %s; channels %s" % (approved, sorted(d["channel"] for d in e["approved"])))
        report("without the rules the oracle fails closed", oracle == "unclassified" and "thread()" in oracle_why
               and bool(bare["diffs"]), "oracle %s: %s" % (oracle, oracle_why))
        for mode, label in (("extra-stdout", "(a) extra stdout line"), ("extra-stderr", "(b) extra stderr line"),
                            ("exit-only", "(b) changed exit status")):
            rc, rep, e = rt_diff(legacy, fake(mode), corpus(mode), policy, args.parse_dump, os.path.join(work, mode + ".json"))
            report(label + ": rejected", rc == 1 and bool(e["diffs"]),
                   "differing channels %s" % sorted(d["channel"] for d in e["diffs"]))

        def one_byte(d):
            path = os.path.join(d, "main.f")
            with open(path, "rb") as handle:
                text = handle.read()
            with open(path, "wb") as handle:
                handle.write(text.replace(b"index 47", b"index 4X", 1))  # a comment byte: same behaviour

        def module(d):
            with open(os.path.join(d, "mod_extra.f"), "w") as handle:
                handle.write("fn(unused()) { return 1 }\nexport(unused)\n")

        for edit, label in ((one_byte, "(c) one-byte source change"), (module, "(d) extra module-input file")):
            rc, rep, e = rt_diff(legacy, vm, corpus(label[1], edit), policy, args.parse_dump, os.path.join(work, label[1] + ".json"))
            report(label + ": rules expire", rc == 1 and bool(e["diffs"]) and sorted(rep["summary"]["unused_policies"]) == sorted(RULES),
                   "differing %s; unused %s" % (sorted(d["channel"] for d in e["diffs"]), rep["summary"]["unused_policies"]))
        rc, rep, e = rt_diff(legacy, vm, corpus("seed1"), policy, args.parse_dump, os.path.join(work, "seed1.json"), seed=1)
        report("(e) under --seed 1: out of scope", sorted(rep["summary"].get("out_of_scope_policies", [])) == sorted(RULES)
               and not rep["summary"]["unused_policies"] and not e["approved"],
               "out of scope %s; approvals %s" % (rep["summary"].get("out_of_scope_policies"), e["approved"]))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("%s g00047 exception controls: %d failure(s)" % ("PASS" if not failures else "FAIL", failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
