#!/usr/bin/env python3
"""Negative controls for comparator_profile.py (owner decision 2026-10-25).

A refused profile may only be reported as UNVERIFIED after the exact
method:sort refusal; everything else must fail rather than skip: a crash, an
internal error, a different refusal, VM execution with wrong output, a missing
or disagreeing compiler, or the milestone switch on a refused binary. A refused
comparator case must show the exact refusal and automatic-mode legacy parity.
The runner is stubbed, so no binary is needed.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import comparator_profile as cp  # noqa: E402

failures = 0


def check(label, ok):
    global failures
    print(("PASS " if ok else "FAIL ") + label)
    failures += 0 if ok else 1


def stats(**kw):
    base = dict(outcome="vm", reason="", vm_executions=1, legacy_executions=0)
    base.update(kw)
    return [base]


VERIFIED = dict(out="[1,2]\n", err="", exit=0, stats=stats())
REFUSED = dict(out="", err="error: $CASE/case.f:2:3: NIFT_VM=require: not compiled for the VM (method:sort)\n",
               exit=1, stats=stats(outcome="require-error", reason="method:sort", vm_executions=0))
HARNESS = dict(out="", err="not compiled: method:sort\n", exit=2,
               stats=stats(outcome="require-error", reason="", vm_executions=0))


def with_run(answers):
    """Stub cp.run: answers maps the NIFT_VM mode to a result (or a function)."""
    def fake(binary, mode, source, root):
        value = answers[mode]
        return value(binary, source) if callable(value) else dict(value)
    cp.run = fake


def profile_raises(label, result, env=None):
    with_run({"require": result})
    saved = dict(os.environ)
    os.environ.update(env or {})
    try:
        cp.profile("nift")
        ok = False
    except cp.ProfileError:
        ok = True
    finally:
        os.environ.clear()
        os.environ.update(saved)
    check(label, ok)


def main():
    os.environ.pop("NIFT_CP14B_CXX", None)
    os.environ.pop("NIFT_CP14B_REQUIRE_COMPARATOR", None)
    with_run({"require": VERIFIED})
    p = cp.profile("nift")
    check("verified probe -> verified", p.found == "verified" and not p.refused)
    with_run({"require": REFUSED})
    p = cp.profile("nift")
    check("exact CLI refusal -> refused", p.refused and p.harness is False)
    with_run({"require": HARNESS})
    p = cp.profile("nift")
    check("exact harness refusal -> refused (harness)", p.refused and p.harness is True)
    DESIGN = dict(out="", err="", exit=1, stats=stats(outcome="require-error", reason="method:sort", vm_executions=0, marker="control"))
    with_run({"require": DESIGN})
    p = cp.profile("nift")
    check("exact design-driver refusal -> refused (design)", p.refused and p.harness == "design")
    profile_raises("silent refusal without a design marker -> failure",
                   dict(DESIGN, stats=stats(outcome="require-error", reason="method:sort", vm_executions=0)))
    profile_raises("design-shaped refusal with another reason -> failure",
                   dict(DESIGN, stats=stats(outcome="require-error", reason="aliasing", vm_executions=0, marker="control")))
    profile_raises("VM ran with wrong output -> failure", dict(VERIFIED, out="[2,1]\n"))
    profile_raises("VM ran twice (replay) -> failure", dict(VERIFIED, stats=stats(vm_executions=2)))
    profile_raises("legacy executed -> failure", dict(VERIFIED, stats=stats(legacy_executions=1)))
    profile_raises("crash -> failure", dict(out="", err="", exit=-11, stats=[]))
    profile_raises("internal error -> failure", dict(out="", err="internal error\n", exit=70,
                                                    stats=stats(outcome="internal-error", vm_executions=0)))
    profile_raises("different refusal reason -> failure",
                   dict(REFUSED, err=REFUSED["err"].replace("method:sort", "aliasing"),
                        stats=stats(outcome="require-error", reason="aliasing", vm_executions=0)))
    profile_raises("refusal with output -> failure", dict(REFUSED, out="x\n"))
    profile_raises("missing statistics -> failure", dict(REFUSED, stats=[]))
    profile_raises("milestone switch on a refused binary -> failure", REFUSED, {"NIFT_CP14B_REQUIRE_COMPARATOR": "1"})
    profile_raises("missing compiler -> failure", REFUSED, {"NIFT_CP14B_CXX": "/nonexistent/c++"})
    # Compiler/probe disagreement (stub the compiler query).
    real = cp.compiler_glibcxx
    for version, result, label in (("20260321", REFUSED, "frozen compiler but refusing binary -> failure"),
                                   ("20240904", VERIFIED, "other compiler but VM-executing binary -> failure")):
        cp.compiler_glibcxx = lambda cxx, v=version: v
        profile_raises(label, result, {"NIFT_CP14B_CXX": "c++"})
    cp.compiler_glibcxx = real
    # refused_ok on a comparator case.
    prof = cp.Profile("refused", False)
    legacy = dict(out="[1,2,3]\n", err="", exit=0, stats=[])
    auto = dict(legacy, stats=stats(outcome="fallback", reason="method:sort", vm_executions=0))
    cases = {
        "exact refusal and legacy parity -> ok": ({"require": REFUSED, "1": auto, "0": legacy}, True),
        "case ran on the VM (silent misexecution) -> failure": ({"require": dict(VERIFIED), "1": auto, "0": legacy}, False),
        "automatic output differs from legacy -> failure": ({"require": REFUSED, "1": dict(auto, out="[3,2,1]\n"), "0": legacy}, False),
        "automatic run did not fall back -> failure": ({"require": REFUSED, "1": dict(legacy, stats=stats()), "0": legacy}, False),
        "automatic stderr differs -> failure": ({"require": REFUSED, "1": dict(auto, err="warning\n"), "0": legacy}, False),
    }
    for label, (answers, want) in cases.items():
        with_run(answers)
        check("refused_ok: " + label, cp.refused_ok(prof, "nift", "legacy", "a.sort(f)", None)[0] is want)
    hprof = cp.Profile("refused", True)
    with_run({"require": HARNESS, "0": legacy})
    check("refused_ok harness: exact refusal, legacy oracle ran -> ok",
          cp.refused_ok(hprof, "san", "legacy", "a.sort(f)", None)[0] is True)
    with_run({"require": HARNESS, "0": dict(out="", err="internal error\n", exit=70, stats=[])})
    check("refused_ok harness: legacy oracle internal error -> failure",
          cp.refused_ok(hprof, "san", "legacy", "a.sort(f)", None)[0] is False)
    for source, want in (("a.sort(1,2)", False), ("a.sort()", False), ("a.sort((x,y)=>x<y)", True),
                         ("a.sort(mk())", True), ("a.sort_by(f)", False), ('a.sort("x,y")', True)):
        check(f"classifier {source!r} -> {want}", cp.is_comparator(source) is want)
    print(("PASS" if not failures else "FAIL") + f" comparator profile controls: {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
