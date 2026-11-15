#!/usr/bin/env python3
"""Selftest of benchmarks/v412_scripting_compare.py (owner decision D-097): a run whose execution is not valid never
produces a timing, and a valid one does.

On the Runtime 2.0 binary (--nift, profile rt2), with 2 timed samples per workload (timings are checked for
presence only, never reported):
  positive              the workloads the oracle accepts (all but the two recorded invalid programs below) PASS
                        with a timing;
  nift-run              the old `nift run <script>` invocation: every script workload FAILED, no timing;
  nift-sh               the old `nift sh` shell invocation: the REPL workload FAILED, no timing;
  unqualified-touch     `touch(` under the rt2 profile (refused by the VM, L6 Option B): fs workload FAILED, no timing;
  wrong-output          one workload's expected stdout altered: FAILED, no timing;
  missing-side-effect   the fs workload expects an eleventh file it never creates: FAILED, no timing;
  rendered              in the text output a FAILED row carries "FAILED" and "(no timing)" and no figure.
With --v412-nift the v4.12.0 binary also gets the positive control under profile v412, and the equivalence proof of
the fs workload is checked.

The two original workloads stream_read_5k_lines and read_val_500 are invalid on both versions as written (legacy:
"cannot change binding type"; the VM refuses them): they are expected FAILED in every run, and are recorded as such.

usage: v412_scripting_compare_selftest.py --nift RT2_BIN [--v412-nift V412_BIN]
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "benchmarks"))
import v412_scripting_compare as B  # noqa: E402

INVALID_AS_WRITTEN = {"stream_read_5k_lines", "read_val_500"}
failures = []


def check(name, ok, why=""):
    print("%s %s%s" % ("PASS" if ok else "FAIL", name, "" if ok else ": " + why))
    if not ok:
        failures.append(name)


def no_timing(results, names):
    return all(results[n]["status"] == "FAILED" and "timing" not in results[n] and results[n].get("reason")
               for n in names)


def edit(name, field, fn):
    index = {"script": 4, "expected": 6, "created": 7}[field]

    def patch(rows):
        out = []
        for r in rows:
            r = list(r)
            if r[0] == name:
                r[index] = fn(r[index])
            out.append(tuple(r))
        return out
    return patch


def positive(nift, profile):
    res = B.run_all(nift, profile, samples=2)
    good = [n for n in res if n not in INVALID_AS_WRITTEN]
    check("positive (%s): valid workloads PASS with a timing" % profile,
          all(res[n]["status"] == "PASS" and res[n]["timing"]["samples"] == 2 for n in good),
          str({n: res[n].get("reason") for n in good if res[n]["status"] != "PASS"}))
    check("positive (%s): the two invalid-as-written workloads are FAILED, no timing" % profile,
          no_timing(res, INVALID_AS_WRITTEN), str({n: res[n] for n in INVALID_AS_WRITTEN}))
    check("positive (%s): categories" % profile,
          res["fs_10_touch_open"]["category"] == "equivalent-operation" and
          all(res[n]["category"] == "identical-source" for n in res if n != "fs_10_touch_open"))
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--v412-nift")
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    before = B.sha256(nift)
    positive(nift, "rt2")
    scripts = [w[0] for w in B.workloads("rt2") if w[2] == "script"]
    res = B.run_all(nift, "rt2", samples=2, prefix=["run"])
    check("nift-run: every script workload FAILED, no timing", no_timing(res, scripts))
    check("nift-run: the reason names the unknown command",
          "unknown command 'run'" in res["statements_2k"]["reason"], res["statements_2k"].get("reason", ""))
    res = B.run_all(nift, "rt2", samples=2, prefix=["sh"],
                    patch=lambda rows: [r for r in rows if r[0] == "repl_500_statements"])
    check("nift-sh: the REPL workload FAILED, no timing", no_timing(res, ["repl_500_statements"]))
    res = B.run_all(nift, "rt2", samples=2,
                    patch=edit("fs_10_touch_open", "script", lambda s: s.replace("native::touch(", "touch(")))
    check("unqualified-touch (rt2): fs workload FAILED, no timing", no_timing(res, ["fs_10_touch_open"]),
          str(res["fs_10_touch_open"]))
    res = B.run_all(nift, "rt2", samples=2, patch=edit("statements_2k", "expected", lambda s: "1\n"))
    check("wrong-output: FAILED, no timing", no_timing(res, ["statements_2k"]))
    res = B.run_all(nift, "rt2", samples=2,
                    patch=edit("fs_10_touch_open", "created", lambda c: dict(c, f11=0)))
    check("missing-side-effect: FAILED, no timing", no_timing(res, ["fs_10_touch_open"]))
    text = B.render(res, "rt2")
    row = next(l for l in text.splitlines() if " fs_10_touch_open" in l)
    check("rendered: a FAILED row says FAILED and (no timing), with no figure",
          row.startswith("FAILED ") and row.endswith("(no timing)") and " ms" not in row, row)
    if a.v412_nift:
        old = os.path.abspath(a.v412_nift)
        positive(old, "v412")
        o, n = B.observe_fs(old, "v412"), B.observe_fs(nift, "rt2")
        check("equivalence: fs workload, same exit, stdout, stderr, files and sizes", o == n and o[0] == 0,
              "%r / %r" % (o, n))
    if B.sha256(nift) != before:
        print("DISCARDED: %s changed during the selftest; rerun" % nift)
        return 3
    print("%s v412 scripting compare selftest: %d failure(s)" % ("FAIL" if failures else "PASS", len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
