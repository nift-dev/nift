#!/usr/bin/env python3
"""B1/B2/B5 end-to-end harness tests with fake nift binaries.

The fake replays the real goldens in legacy mode and emulates a correct
implementation in VM mode, so a baseline passes 80/80. Each scenario injects
one adversarial behaviour. The tests invoke the real run.py (not a fake
checker), so the mutant return type, crash handling and rule fields are
exercised end to end.
"""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TEST = HERE.parent
RUN = TEST / "run.py"
TEMPLATE = HERE / "fake_nift.py.in"
MODES = [
    "ideal", "detect_req", "detect_control", "fallback_all", "refuse_all",
    "nostats", "malformed", "missing_exec_field", "both_engines",
    "crash_after", "asan_after", "exit70_after", "nonreq_harness", "nonreq_fallback",
    "timeout", "checked_stale", "checked_wrong", "asan_uaf", "asan_clean",
    "fallback_exec_2", "fallback_exec_3", "fallback_exec_69",
]


def make_fakes(folder, mutant):
    folder.mkdir(parents=True, exist_ok=True)
    text = TEMPLATE.read_text().replace("__TESTDIR__", str(TEST)).replace("__MUTANT__", str(mutant))
    out = {}
    for mode in MODES:
        path = folder / f"nift-{mode}"
        path.write_text(text.replace("__MODE__", mode))
        path.chmod(0o755)
        out[mode] = str(path)
    return out


def invoke(args, timeout_env="5"):
    env = dict(os.environ)
    env["NIFT_CP13B_TIMEOUT"] = timeout_env
    return subprocess.run([sys.executable, str(RUN)] + args, env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                          timeout=120)


def expect_pass(result, mode, needle="PASS exception mutant"):
    if result.returncode != 0 or needle not in result.stdout:
        print(f"FAIL positive control {mode}: rc={result.returncode}\n{result.stdout[-2000:]}")
        return False
    return True


def expect_fail(result, mode):
    if result.returncode == 0 or "PASS exception mutant" in result.stdout:
        print(f"FAIL negative control {mode}: rc={result.returncode}\n{result.stdout[-2000:]}")
        return False
    return True


def main():
    failures = 0
    checks = 0
    with tempfile.TemporaryDirectory(prefix="cp13b-adversarial-") as folder:
        folder = Path(folder)
        fakes = make_fakes(folder / "m1", 1)
        fakes2 = make_fakes(folder / "m2", 2)
        fakes3 = make_fakes(folder / "m3", 3)
        fakes7 = make_fakes(folder / "m7", 7)
        ideal = fakes["ideal"]
        # Baseline must genuinely pass.
        base = invoke(["--nift", ideal, "--mode", "future"])
        checks += 1
        if base.returncode != 0 or "PASS 80, FAIL 0" not in base.stdout:
            print(f"FAIL fake baseline: rc={base.returncode}\n{base.stdout[-2000:]}")
            failures += 1
        else:
            print("ok   fake baseline: PASS 80, FAIL 0")
        # B1 positive control: a semantic difference in every E1 required case.
        good = invoke(["--nift", fakes["detect_req"], "--oracle", ideal, "--mutant", "1"])
        checks += 1
        if not expect_pass(good, "detect_req"):
            failures += 1
        else:
            print("ok   positive control: detect_req PASS")
            for case in ("uncatchable_index", "s13_global_take_index", "throw_non_error"):
                if case not in good.stdout:
                    print(f"FAIL positive control: {case} was not reported detected")
                    failures += 1
                    break
        # B1/B2/B4 negative controls against a passing baseline.
        for mode in ("ideal", "fallback_all", "refuse_all", "nostats", "malformed",
                     "missing_exec_field", "both_engines", "crash_after", "asan_after",
                     "exit70_after",
                     "nonreq_harness", "nonreq_fallback", "timeout", "detect_control"):
            checks += 1
            result = invoke(["--nift", fakes[mode], "--oracle", ideal, "--mutant", "1"])
            if not expect_fail(result, mode):
                failures += 1
            else:
                print(f"ok   negative control: {mode} rejected")
        # R2-1: a designated fallback case that unexpectedly executes with an
        # exit status in 2..69 must be a harness failure, not a detection.
        for mode in ("fallback_exec_2", "fallback_exec_3", "fallback_exec_69"):
            checks += 1
            result = invoke(["--nift", fakes3[mode], "--oracle", ideal, "--mutant", "3"])
            if not expect_fail(result, mode) or "HARNESS" not in result.stdout:
                print(f"FAIL R2-1 control {mode}: unexpected execution not classified as harness")
                failures += 1
            else:
                print(f"ok   R2-1 control {mode}: unexpected executed fallback rejected as harness")
        # B5 checked-build path.
        checks += 1
        checked = invoke(["--nift", fakes2["checked_stale"], "--oracle", ideal,
                          "--checked-mutant", "2"])
        if not expect_pass(checked, "checked_stale", "PASS checked mutant"):
            failures += 1
        else:
            print("ok   checked positive: stale exception handler accepted")
        checks += 1
        if not expect_fail(invoke(["--nift", fakes2["checked_wrong"], "--oracle", ideal,
                                   "--checked-mutant", "2"]), "checked_wrong"):
            failures += 1
        else:
            print("ok   checked negative: wrong internal reason rejected")
        # B5 ASan path.
        checks += 1
        asan = invoke(["--nift", fakes7["asan_uaf"], "--oracle", ideal, "--asan-mutant", "7"])
        if not expect_pass(asan, "asan_uaf", "PASS asan mutant"):
            failures += 1
        else:
            print("ok   ASan positive: heap-use-after-free in op_get_upvalue accepted")
        checks += 1
        if not expect_fail(invoke(["--nift", fakes7["asan_clean"], "--oracle", ideal,
                                   "--asan-mutant", "7"]), "asan_clean"):
            failures += 1
        else:
            print("ok   ASan negative: clean run rejected")
        # --oracle is mandatory.
        checks += 1
        missing = invoke(["--nift", fakes["detect_req"], "--mutant", "1"])
        if missing.returncode != 2:
            print(f"FAIL --oracle mandatory: rc={missing.returncode}\n{missing.stdout[-1000:]}")
            failures += 1
        else:
            print("ok   --oracle mandatory in mutant mode")
    print(f"{'PASS' if failures == 0 else 'FAIL'} adversarial harness self-test: "
          f"{checks} checks, {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
