#!/usr/bin/env python3
"""BH3 curated guard mutation / test-of-test harness.

For each curated guard, apply a mutation family to an exact copy, run the
mutated guard against a real Nift binary (or a sabotaged/stub substitute that
simulates a broken tool), run the BH2 static scanner over the mutant, and
classify whether the mutation is LIVE (the guard falsely greens / its
load-bearing check is gone) or ROBUST (the guard still correctly goes
non-green). The retained JSON report is the BH3 evidence for the tranche.

Mutations are described declaratively below so a reviewer can reproduce each
case by hand.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def _run(cmd: list[str], cwd: Path | None = None) -> tuple[int, str]:
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=str(cwd or Path.cwd()),
                           timeout=180)
    except subprocess.TimeoutExpired as exc:
        return 124, (exc.stdout or "") + (exc.stderr or "")
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def make_stub_nift(path: Path) -> None:
    """A fake `nift` that always exits 0 and produces nothing."""
    path.write_text("#!/bin/sh\nexit 0\n")
    path.chmod(0o755)


def make_sabotaged_nift(path: Path, real: Path) -> None:
    """A wrapper around the real `nift` that, after `build --all`, drops a
    spurious file into `public/` — simulating an incremental-vs-clean bug the
    pagination guard must detect."""
    body = (
        "#!/bin/sh\n"
        f"real={real}\n"
        '"$real" "$@"\n'
        'rc=$?\n'
        'if [ "$rc" = 0 ] && [ "$1" = "build", "--all" ] && [ -d public ]; then\n'
        '  echo sabotage >> public/sabotage-marker\n'
        'fi\n'
        'if [ "$rc" = 0 ] && [ "$1" = "build" ] && [ -d public ]; then\n'
        '  rm -f public/sabotage-marker\n'
        'fi\n'
        'exit $rc\n'
    )
    path.write_text(body)
    path.chmod(0o755)


def mut_pagination_remove_check(text: str) -> str:
    return text.replace("    if inc != clean:", "    if False:")


def mut_pagination_invert_check(text: str) -> str:
    return text.replace("    if inc != clean:", "    if inc == clean:")


def mut_contracts_remove_expect_failures(text: str) -> str:
    out = []
    skip = False
    for line in text.splitlines(keepends=True):
        if any(k in line for k in ("expect_build_failure", "expect_open_failure",
                                   "expect_config_failure")):
            skip = True
            continue
        if skip:
            if line.startswith("    ") and line.strip():
                continue
            skip = False
        out.append(line)
    return "".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True, help="path to the real Nift binary")
    ap.add_argument("--output", required=True, type=Path)
    args = ap.parse_args()
    real = str(Path(args.nift).resolve())
    repo = Path(__file__).resolve().parents[1]

    guards = [
        {
            "id": "pagination.incremental-clean-equivalence",
            "guard": "tests/pagination_incremental_equivalence.py",
            "baseline": ["python3", "tests/pagination_incremental_equivalence.py", "--nift", real],
            "mutants": [
                {
                    "name": "m2-stub-nift",
                    "family": "M2 input-redirection",
                    "binary": "stub",
                    "apply": None,
                    "expected": "GREEN",
                    "expectation": "a stub nift (validates nothing) must not be accepted as a green run; if it is, the guard is LIVE_FALSE_GREEN",
                },
                {
                    "name": "m3-invert-equivalence-check",
                    "family": "M3 condition-inversion",
                    "binary": "real",
                    "apply": mut_pagination_invert_check,
                    "expected": "NON_GREEN",
                    "expectation": "inverting the load-bearing check must flip a healthy run to FAIL (proves the check is load-bearing)",
                },
                {
                    "name": "m4-remove-equivalence-check-sabotaged",
                    "family": "M4 check-removal / test-of-test",
                    "binary": "sabotaged",
                    "apply": mut_pagination_remove_check,
                    "expected": "GREEN",
                    "expectation": "removing the equivalence check must turn a would-be RED (sabotaged nift) into GREEN: the removal is LIVE",
                },
            ],
        },
        {
            "id": "contracts.namespace-reservation",
            "guard": "tests/contracts_smoke.sh",
            "baseline": ["bash", "tests/contracts_smoke.sh"],
            "mutants": [
                {
                    "name": "m2-stub-nift",
                    "family": "M2 input-redirection",
                    "binary": "stub",
                    "apply": None,
                    "expected": "NON_GREEN",
                    "expectation": "a stub nift must be detected (guard stays RED), not silently accepted; if so, ROBUST",
                },
                {
                    "name": "m4-remove-expect-failure-probes-stub",
                    "family": "M4 check-removal / test-of-test",
                    "binary": "stub",
                    "apply": mut_contracts_remove_expect_failures,
                    "expected": "NON_GREEN",
                    "expectation": "even with the expected-failure probes removed the stub must not be accepted as green; the probes are not the sole load-bearing check",
                },
            ],
        },
    ]

    results = []
    with tempfile.TemporaryDirectory(prefix="bh3-mutation.") as tmp:
        tmp = Path(tmp)
        stub = tmp / "stub_nift"
        make_stub_nift(stub)
        sabotaged = tmp / "sabotaged_nift"
        make_sabotaged_nift(sabotaged, Path(real))
        for guard in guards:
            src = repo / guard["guard"]
            baseline_rc, baseline_out = _run(guard["baseline"], cwd=repo)
            entry = {
                "guard_id": guard["id"],
                "guard": guard["guard"],
                "baseline": {"exit": baseline_rc, "green": baseline_rc == 0,
                             "outcome_tail": baseline_out.strip().splitlines()[-1] if baseline_out.strip() else ""},
                "mutants": [],
            }
            for mut in guard["mutants"]:
                binary = {"stub": str(stub), "real": real, "sabotaged": str(sabotaged)}[mut["binary"]]
                mc = tmp / f"{Path(guard['guard']).name}.{mut['name']}"
                shutil.copy2(src, mc)
                if mut["apply"] is not None:
                    mc.write_text(mut["apply"](_read(mc)))
                if guard["id"].startswith("contracts"):
                    run_cmd = ["bash", str(mc)]
                    env = {"NIFT_BIN": binary}
                else:
                    run_cmd = ["python3", str(mc), "--nift", binary]
                    env = {}
                rc, out = _run_with_env(run_cmd, repo, env)
                scan_rc, _ = _run(
                    ["python3", str(repo / "scripts" / "test_integrity_check.py"),
                     str(mc), "--output", str(tmp / f"{mc.name}.scan.json")],
                    cwd=repo,
                )
                green = rc == 0
                if mut["expected"] == "GREEN":
                    classification = "LIVE_FALSE_GREEN" if green else "HYPOTHESIS_REJECTED"
                else:
                    classification = "ROBUST" if not green else "LIVE_FALSE_GREEN"
                entry["mutants"].append({
                    "name": mut["name"],
                    "family": mut["family"],
                    "binary": mut["binary"],
                    "expectation": mut["expectation"],
                    "expected": mut["expected"],
                    "classification": classification,
                    "exit": rc,
                    "outcome": "PASS" if rc == 0 else ("FAIL" if rc == 1 else f"EXIT_{rc}"),
                    "outcome_tail": out.strip().splitlines()[-1] if out.strip() else "",
                    "bh2_scanner_findings": scan_rc,
                })
            results.append(entry)

    report = {
        "schema_version": 1,
        "campaign": "bh3",
        "checkpoint": "guard-mutation-tranche-1",
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "platform": "Linux (BH3 tranche 1)",
        "nift": real,
        "purpose": "BH3 curated guard mutation / test-of-test tranche 1: representative runtime (stub/sabotaged binary) and source mutations against exact guard copies, with the BH2 static scanner run over each mutant.",
        "guards": results,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"report={args.output}")
    return 0


def _run_with_env(cmd: list[str], cwd: Path, env: dict) -> tuple[int, str]:
    import os
    full = dict(os.environ)
    full.update(env)
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=str(cwd), env=full,
                           timeout=180)
    except subprocess.TimeoutExpired as exc:
        return 124, (exc.stdout or "") + (exc.stderr or "")
    return p.returncode, (p.stdout or "") + (p.stderr or "")


if __name__ == "__main__":
    raise SystemExit(main())
