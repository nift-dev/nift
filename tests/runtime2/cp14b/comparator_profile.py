"""CP14B comparator toolchain profile (owner decision, 2026-10-25).

`sort(comparator)` is compiled for the VM only when the binary was built
against the libstdc++ whose comparator schedule D4 froze
(`__GLIBCXX__ == 20260321`, GCC 15.2); elsewhere the compiler refuses it with
`method:sort` before execution and legacy runs the program (production gate in
src/rt/compiler_collection_sort.cpp, unchanged here).

profile(nift) decides which profile a binary has with one narrow admission
probe, never from a failed test:
  verified  the probe runs on the VM (outcome vm, one VM execution, no legacy
            execution) and prints exactly the sorted array;
  refused   the probe is refused before execution with exactly
            "NIFT_VM=require: not compiled for the VM (method:sort)" and VM
            statistics outcome require-error / reason method:sort.
Anything else raises ProfileError: a crash, a missing binary, an internal
error, an unexpected refusal or VM execution with the wrong output.

When the compiler that built the binary is named (NIFT_CP14B_CXX, set by the
Makefile), its __GLIBCXX__ must agree with the probe: verified exactly when it
is the frozen 20260321. NIFT_CP14B_REQUIRE_COMPARATOR=1 (the milestone GCC 15.2
run) makes a refused profile a failure.

On a refused profile, refused_ok() checks a comparator case: the exact
`method:sort` refusal under NIFT_VM=require, and the automatic-mode run equal
to legacy byte for byte (stdout, stderr, exit), with the statistics showing
that fallback. Scripts then report UNVERIFIED COMPARATOR COVERAGE: refusal-only
evidence is never full comparator certification.
"""
import os
import re
import subprocess
import tempfile
from pathlib import Path

from functional import run

FROZEN_GLIBCXX = "20260321"
PROBE = "a := [2, 1]\na.sort((x, y) => x < y)\nprint(a.stringify())"
REFUSAL = re.compile(r"\Aerror: \$CASE/case\.f:\d+:\d+: NIFT_VM=require: not compiled for the VM \(method:sort\)\n\Z")
# A comparator case: a .sort( call with exactly one top-level argument (the
# arity the production gate guards; zero or two arguments never reach it).
# Misclassification cannot pass: a tagged non-comparator case must then show
# the exact refusal, and an untagged comparator case keeps its full VM
# assertions.
SORT_CALL = ".sort("


class ProfileError(Exception):
    pass


def _arity(source, open_at):
    depth, args, seen, quote = 0, 0, False, None
    i = open_at
    while i < len(source):
        ch = source[i]
        if quote:
            if ch == "\\":
                i += 1
            elif ch == quote:
                quote = None
        elif ch in "\"'":
            quote, seen = ch, True
        elif ch in "([{":
            depth += 1
            if depth > 1:
                seen = True
        elif ch in ")]}":
            depth -= 1
            if depth == 0:
                return args + (1 if seen else 0)
        elif ch == "," and depth == 1:
            args += 1
        elif not ch.isspace():
            seen = True
        i += 1
    return None


def is_comparator(source):
    start = 0
    while True:
        at = source.find(SORT_CALL, start)
        if at < 0:
            return False
        if _arity(source, at + len(SORT_CALL) - 1) == 1:
            return True
        start = at + 1


def compiler_glibcxx(cxx):
    try:
        out = subprocess.run([cxx, "-x", "c++", "-dM", "-E", "-"], input="#include <cstddef>\n",
                             capture_output=True, text=True, timeout=60, check=True).stdout
    except (OSError, subprocess.SubprocessError) as error:
        raise ProfileError(f"cannot query {cxx} for __GLIBCXX__: {error}")
    match = re.search(r"#define __GLIBCXX__ (\d+)", out)
    return match.group(1) if match else None


HARNESS_REFUSAL = "not compiled: method:sort\n"  # the VM-only sanitizer/drain harnesses (no legacy engine)


def _refused(result, harness):
    st = result["stats"][-1] if result["stats"] else {}
    if harness:
        shape = result["exit"] == 2 and result["err"] == HARNESS_REFUSAL
    else:
        shape = result["exit"] == 1 and bool(REFUSAL.match(result["err"])) and st.get("reason") == "method:sort"
    return (shape and result["out"] == "" and st.get("outcome") == "require-error" and
            st.get("vm_executions") == 0 and st.get("legacy_executions") == 0)


class Profile:
    def __init__(self, found, harness):
        self.found, self.harness = found, harness

    @property
    def refused(self):
        return self.found == "refused"


def profile(nift):
    """Profile('verified'|'refused', harness): harness is True for the VM-only
    sanitizer harness, recognised only by its own exact refusal shape."""
    with tempfile.TemporaryDirectory(prefix="cp14b-comparator-profile-") as tmp:
        result = run(nift, "require", PROBE, Path(tmp))
    st = result["stats"][-1] if result["stats"] else {}
    if (result["exit"] == 0 and result["out"] == "[1,2]\n" and result["err"] == "" and
            st.get("outcome") == "vm" and st.get("vm_executions") == 1 and st.get("legacy_executions") == 0):
        found, harness = "verified", None
    elif _refused(result, harness=False):
        found, harness = "refused", False
    elif _refused(result, harness=True):
        found, harness = "refused", True
    else:
        raise ProfileError(f"comparator probe neither ran on the VM nor was refused with method:sort: {result!r}")
    cxx = os.environ.get("NIFT_CP14B_CXX")
    if cxx:
        version = compiler_glibcxx(cxx)
        expected = "verified" if version == FROZEN_GLIBCXX else "refused"
        if found != expected:
            raise ProfileError(f"comparator probe says {found} but {cxx} has __GLIBCXX__={version} "
                               f"(expected {expected})")
    if found == "refused" and os.environ.get("NIFT_CP14B_REQUIRE_COMPARATOR") == "1":
        raise ProfileError("NIFT_CP14B_REQUIRE_COMPARATOR=1: this binary refuses sort(comparator); the "
                           "milestone run needs the frozen GCC 15.2/libstdc++ 20260321 profile")
    return Profile(found, harness)


def refused_ok(prof, nift, legacy, source, root):
    """(ok, detail) for a comparator case on a refused profile."""
    require = run(nift, "require", source, root)
    if not _refused(require, prof.harness):
        return False, dict(reason="expected the exact method:sort refusal", require=require)
    if prof.harness:  # no legacy engine inside the harness; legacy runs on the CLI oracle
        old = run(legacy, "0", source, root)
        ok = isinstance(old["exit"], int) and old["exit"] in (0, 1) and "internal" not in old["err"]
        return ok, dict(require=require, legacy=old)
    auto = run(nift, "1", source, root)
    old = run(legacy, "0", source, root)
    st = auto["stats"][-1] if auto["stats"] else {}
    same = all(auto[k] == old[k] for k in ("out", "err", "exit"))
    fell_back = st.get("outcome") == "fallback" and st.get("reason") == "method:sort" and \
        st.get("vm_executions") == 0
    return same and fell_back, dict(require=require, automatic=auto, legacy=old)


def banner(prof, unverified, script):
    if not prof.refused:
        return f"COMPARATOR PROFILE verified (__GLIBCXX__ {FROZEN_GLIBCXX}): {script} comparator cases ran on the VM"
    return (f"SKIP UNVERIFIED COMPARATOR COVERAGE: {script}: {unverified} comparator case(s) refused with "
            f"method:sort on an unverified libstdc++ profile; exact refusal and legacy parity checked; "
            f"this is not comparator certification (milestone needs the GCC 15.2/20260321 run)")
