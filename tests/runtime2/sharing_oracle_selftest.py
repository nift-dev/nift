#!/usr/bin/env python3
"""Self-test of the rt-diff sharing oracle (B07 option 2, owner 2026-11-10; combined-b3-b5.md
section 60; tools/rt-diff/sharing_oracle.py).

Runs rt-diff with --sharing-oracle over probe programs, legacy (NIFT_VM=0) against
  - the real VM (semantic probes and the positive controls),
  - tests/runtime2/sharing_oracle_fake_vm.py (negative controls: each plants one unrelated VM
    bug that hits P and T(P) alike), and
  - the existing rt-diff mutants (tests/runtime2/rt_diff_mutant.py over the real VM),
and requires each program's classification, including WHICH check rejects it. The oracle shows
attribution, not correctness; these controls prove that it approves nothing else.

usage: sharing_oracle_selftest.py --nift NIFT [--parse-dump .build/rt-parse-dump]
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

SHARE = "fn(add9(xs)) { xs.push(99); return xs.length() }\na := [1, 2]\nprint(add9(a))\nprint(a.stringify())\n"
NOSHARE = "fn(twice(n)) { return n * 2 }\nprint(twice(4))\nprint(\"done\")\n"
ERRPOS = "fn(put(xs)) { xs.push(7); return 0 }\na := [1]\nprint(put(a))\nprint(a.stringify())\nprint(a[9])\n"

# name -> (source, expected status, required reason substring or None)
#   status: approved | unclassified | same (no difference at all: never sent to the oracle)
#   "not-approved" accepts same or unclassified (a cycle store may be refused identically).
PROBES = {
    # semantic probes, real VM
    "p_direct": (SHARE, "approved", None),
    "p_nested": ("fn(poke(xs)) { inner := xs[1]; inner.push(9); return xs.length() }\na := [1, [2]]\n"
                 "print(poke(a))\nprint(a.stringify())\n", "approved", None),
    "p_nested_object": ("fn(deep(o)) { arr := o[\"list\"]; arr.push(3); return 0 }\nd := {\"list\": [1]}\n"
                        "print(deep(d))\nprint(d.stringify())\n", "approved", None),
    "p_alias": ("fn(two(p, q)) { p.push(1); return q.length() }\nx := [0]\nprint(two(x, x))\n"
                "print(x.stringify())\n", "approved", None),
    "p_returns_arg": ("fn(ident(xs)) { return xs }\na := [1]\nb := ident(a)\nb.push(2)\nprint(a.stringify())\n",
                      "approved", None),
    "p_error_same": (ERRPOS, "approved", None),
    "p_cycle": ("fn(loop(xs)) { xs.push(xs); return 0 }\na := [1]\nprint(loop(a))\nprint(a.stringify())\n",
                "not-approved", None),
    "p_closure_captures_arg": ("fn(mk(xs)) { return (v) => xs.push(v) }\na := [1]\nadd := mk(a)\nadd(5)\n"
                               "print(a.stringify())\n", "unclassified", "closure captures parameter(s) xs"),
    "p_same_builtin": (SHARE + "print(same(a, a))\n", "unclassified", "built-in same()"),
    "p_handle": (SHARE + "h := file(\"x.txt\")\nprint(h.path())\n", "unclassified",
                 "built-in file()"),
    "p_struct": ("struct(P) { v := 1 }\n" + SHARE, "unclassified", "node kinds outside"),
    # review of C67-C71 (Lows): cycle trigger, shadowing, concurrency, imported modules
    "p_cycle_store": ("fn(link(x, y)) { x.push(y); return 0 }\na := [1]\nb := [a]\nprint(link(a, b))\n"
                      "print(a.length())\n", "unclassified", "reports a reference cycle"),
    "p_param_shadows_fn": (SHARE + "fn(g(add9)) { return add9 }\nprint(g(1))\n", "not-approved", None),
    "p_async_task": ("@fn[async](af(n)) { return n }\nfu := af(1)\nprint(await fu)\n" + SHARE, "unclassified",
                     None),
    "p_thread_call": (SHARE + "t := thread(() => 1)\n", "unclassified", None),
    "p_module_clean": ({"main.f": "import(\"m.f\")\n" + SHARE, "m.f": "fn(triple(n)) { return n * 3 }\nexport(triple)\n"},
                       "approved", None),
    "p_module_struct": ({"main.f": "import(\"m.f\")\n" + SHARE, "m.f": "struct(Q) { v := 1 }\nfn(mk()) { return 1 }\nexport(mk)\n"},
                        "unclassified", "module m.f"),
    "p_module_missing": ({"main.f": "import(\"absent.f\")\n" + SHARE}, "not-approved", None),
    # checkpoint 1 (combined-b3-b5.md section 95): the assignment family through arguments and not
    "p_member_store_arg": ('fn(setk(o)) { o["k"] = 2; o["n"] = 3; return 0 }\nd := {"k": 1}\nprint(setk(d))\n'
                           "print(d.stringify())\n", "approved", None),
    "p_nested_store_arg": ("fn(poke(g)) { g[0][0] += 5; g[1][0] = 7; return 0 }\na := [[1], [2]]\n"
                           "print(poke(a))\nprint(a.stringify())\n", "approved", None),
    "p_alias_store_no_call": ('o := {"k": 1}\np := o\np["k"] = 2\nprint(o.stringify())\n', "unclassified",
                              "nothing to rewrite"),
    "p_compound_order": ("fn(f(x)) { i = 1; return x }\na := [1, 2]\ni := 0\na[i] += f(5)\nprint(a.stringify())\n",
                         "unclassified", "check 2"),
    "p_member_cycle_arg": ('fn(link(o, v)) { o["k"] = v; return 0 }\nd := {"k": 1}\nb := [d]\n'
                           "try { print(link(d, b)) } catch (e) { print(e.code) }\nprint(d.size())\n",
                           "unclassified", "reports a reference cycle"),
    # negative controls
    "n_unrelated_stdout": ("// fake: extra-stdout\n" + NOSHARE, "unclassified", "check 2"),
    "n_sharing_plus_unrelated": ("// fake: extra-stdout\n" + SHARE, "unclassified", "check 2"),
    "n_wrong_error_position": ("// fake: error-column\n" + ERRPOS, "unclassified", "check 2"),
    "n_exit_only": ("// fake: exit-only\n" + NOSHARE, "unclassified", "check 2"),
    "n_exit_only_sharing": ("// fake: exit-only\n" + SHARE, "unclassified", "check 2"),
    "n_refusal": ("// fake: refusal\n" + SHARE, "unclassified", "check 4"),
    "n_abort": ("// fake: abort\n" + SHARE, "unclassified", "check 4"),
    # O-1: a VM bug only on the direct-argument code paths (bare arguments): absent under T and T'
    "n_direct_path_only_bug": ("// fake: direct-path\n" + SHARE, "unclassified", "check 6"),
    "n_rewrite_changes_legacy": ("fn(put(xs)) { xs.push(7); return 0 }\na := [1]\nprint(put(a))\n"
                                 "print(a.stringify())\nprint(put(a) + a[9])\n", "unclassified", "check 1"),
}
# Existing rt-diff mutants (rt_diff_mutant.py MODE) over the real VM: no approval may result.
MUTANT_MODES = ("stdout-only", "stderr-only", "exit-only")


def wrapper(path, body):
    with open(path, "w") as handle:
        handle.write("#!/bin/sh\n" + body + "\n")
    os.chmod(path, 0o755)
    return path


def run_rt_diff(a, b, corpus, dump, report, timeout=60):
    cmd = [sys.executable, "-B", os.path.join(REPO, "tools", "rt-diff", "rt-diff"), "--a", a, "--b", b,
           "--corpus", corpus, "--quiet", "--b-vm-stats", "--sharing-oracle", dump, "--report", report,
           "--timeout", str(timeout), "--jobs", "4"]
    proc = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True)
    if proc.returncode not in (0, 1) or not os.path.exists(report):
        sys.exit("FAIL rt-diff harness error (exit %d): %s" % (proc.returncode, proc.stderr[-2000:]))
    with open(report) as handle:
        return json.load(handle)


def status_of(result):
    oracle = result.get("sharing_oracle")
    if not result["diffs"] and not result["approved"]:
        return "same", ""
    if oracle is None:
        return "unclassified-no-oracle", ""
    return oracle["status"], oracle.get("reason", "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--parse-dump", default=os.environ.get("VM_PARSE_DUMP", os.path.join(REPO, ".build", "rt-parse-dump")))
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    work = tempfile.mkdtemp(prefix="sharing-oracle-selftest-")
    failures = 0
    try:
        legacy = wrapper(os.path.join(work, "nift-legacy"), "NIFT_VM=0\nexport NIFT_VM\nexec '%s' \"$@\"" % nift)
        fake = wrapper(os.path.join(work, "nift-fake-vm"), "exec '%s' -B '%s' '%s' \"$@\"" % (
            sys.executable, os.path.join(HERE, "sharing_oracle_fake_vm.py"), nift))
        corpus = os.path.join(work, "probes")
        os.makedirs(corpus)
        for name, (source, _, _) in PROBES.items():
            if isinstance(source, dict):  # a program with modules: its own directory
                os.makedirs(os.path.join(corpus, name))
                for fname, text in source.items():
                    with open(os.path.join(corpus, name, fname), "w") as handle:
                        handle.write(text)
                with open(os.path.join(corpus, name, "rt-diff.only"), "w") as handle:
                    handle.write("main.f\n")
                continue
            with open(os.path.join(corpus, name + ".f"), "w") as handle:
                handle.write(source)
        report = run_rt_diff(legacy, fake, corpus, args.parse_dump, os.path.join(work, "probes.json"))
        seen = {}
        for r in report["results"]:
            stem = os.path.basename(r["path"])[:-2]
            seen[os.path.basename(os.path.dirname(r["path"])) if stem == "main" else stem] = r
        for name, (_, want, reason) in PROBES.items():
            r = seen.get(name)
            if r is None:
                print("FAIL %s: missing from the rt-diff report" % name)
                failures += 1
                continue
            got, why = status_of(r)
            ok = (got in ("same", "unclassified") if want == "not-approved" else got == want)
            if ok and reason and reason not in why:
                ok = False
            if any(d["policy"] == "b5-sharing-oracle" for d in r["approved"]) != (got == "approved"):
                ok = False
            print("%s %-26s %-13s %s" % ("PASS" if ok else "FAIL", name, got, why or "-"))
            failures += 0 if ok else 1
        # The rewrite itself: exactly the call arguments of declared functions, deepcopy (never
        # copy), and fail closed when nothing can be proved.
        sys.path.insert(0, os.path.join(REPO, "tools", "rt-diff"))
        import sharing_oracle
        static = [
            ("wraps", "fn(f(x, y)) { return x }\na := [1]\nprint(f(a, [a, 2]))\nprint(a.length())\n",
             "fn(f(x, y)) { return x }\na := [1]\nprint(f(deepcopy(a), deepcopy([a, 2])))\nprint(a.length())\n"),
            ("nested-calls", "fn(f(x)) { return x }\nprint(f(f([1])))\n",
             "fn(f(x)) { return x }\nprint(f(deepcopy(f(deepcopy([1])))))\n"),
            ("no-call-site", "a := [1]\na.push(2)\nprint(a.stringify())\n", None),
            ("unparsable", "fn(f(x) { return\n", None),
            ("param-shadows", "fn(f(x)) { return x }\nfn(g(f)) { return f }\nprint(f([1]))\n", None),
        ]
        for name, source, expected in static:
            path = os.path.join(work, "static-%s.f" % name)
            with open(path, "w") as handle:
                handle.write(source)
            root, why = sharing_oracle.parse(args.parse_dump, path)
            got = None if root is None else sharing_oracle.transform(source.encode(), root)[0]
            ok = (got == expected.encode()) if expected else got is None
            ok = ok and (expected is None or b"copy(" not in got.replace(b"deepcopy(", b""))
            print("%s static %-14s %s" % ("PASS" if ok else "FAIL", name, "rewritten as expected" if expected and ok
                                          else (why or "no rewrite (fail closed)") if ok else repr(got)))
            failures += 0 if ok else 1
        # A timeout (review of C67-C71): the VM side hangs past rt-diff's timeout; check 4.
        hang_corpus = os.path.join(work, "hang")
        os.makedirs(hang_corpus)
        with open(os.path.join(hang_corpus, "n_timeout.f"), "w") as handle:
            handle.write("// fake: hang\n" + SHARE)
        rep = run_rt_diff(legacy, fake, hang_corpus, args.parse_dump, os.path.join(work, "hang.json"), timeout=4)
        got, why = status_of(rep["results"][0])
        ok = got == "unclassified" and "timed out" in why
        print("%s %-26s %-13s %s" % ("PASS" if ok else "FAIL", "n_timeout", got, why or "-"))
        failures += 0 if ok else 1
        # The existing rt-diff mutants over the real VM: nothing may be approved by the oracle.
        sharing = [n for n, (src, want, _) in PROBES.items() if want == "approved" and isinstance(src, str)]
        mutant_corpus = os.path.join(work, "mutant-probes")
        os.makedirs(mutant_corpus)
        for name in sharing:
            shutil.copy(os.path.join(corpus, name + ".f"), mutant_corpus)
        for mode in MUTANT_MODES:
            b = wrapper(os.path.join(work, "nift-mutant-" + mode), "NIFT_VM=1\nexport NIFT_VM\nexec '%s' -B '%s' %s '%s' \"$@\"" % (
                sys.executable, os.path.join(HERE, "rt_diff_mutant.py"), mode, nift))
            rep = run_rt_diff(legacy, b, mutant_corpus, args.parse_dump, os.path.join(work, "mutant-%s.json" % mode))
            mutated = [r for r in rep["results"] if r["diffs"] or r["approved"]]
            approved = [r["id"] for r in rep["results"] if any(d["policy"] == "b5-sharing-oracle" for d in r["approved"])]
            # Each mutant must touch at least one program, and wherever its mutation shows, the
            # oracle must not approve: a program whose mutation stayed invisible (e.g. no stderr
            # to mutate) is the plain B5 difference and may be approved.
            touched = [r["id"] for r in rep["results"]
                       if (r.get("sharing_oracle") or {}).get("status") == "unclassified"]
            wrongly = [i for i in approved if mode_touches(mode, os.path.join(mutant_corpus, os.path.basename(i)), nift)]
            ok = bool(touched) and not wrongly and bool(mutated)
            print("%s rt-diff mutant %-12s rejected %d program(s); oracle approvals where the mutation shows: %s"
                  % ("PASS" if ok else "FAIL", mode, len(touched), wrongly or "none"))
            failures += 0 if ok else 1
    finally:
        shutil.rmtree(work, ignore_errors=True)
    total = len(PROBES) + len(MUTANT_MODES) + 5 + 1
    print("%s sharing oracle selftest: %d of %d checks failed" % ("FAIL" if failures else "PASS", failures, total))
    return 1 if failures else 0


def mode_touches(mode, script, nift):
    """Whether rt_diff_mutant MODE changes what rt-diff sees for this program on the VM."""
    proc = subprocess.run([nift, os.path.basename(script)], cwd=os.path.dirname(script), capture_output=True,
                          env=dict(os.environ, NIFT_VM="1"))
    if mode == "stdout-only":
        return bool(proc.stdout)
    if mode == "stderr-only":
        return bool(proc.stderr)
    if mode == "exit-only":
        return proc.returncode != 0
    return True


if __name__ == "__main__":
    sys.exit(main())
