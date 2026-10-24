#!/usr/bin/env python3
"""CP17 routing tripwire: build, page and hook execution stay on legacy.

The VM is selected only by standalone scripts (run_script_on_vm). This gate
fails if a protected path ever writes VM execution statistics. A page/hook
source using a construct the VM refuses under NIFT_VM=require also proves that
the path really ran on legacy: the page output and hook marker must exist.
Missing statistics alone is never treated as proof.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

FAILS = []


def check(ok, label, detail=""):
    print(("PASS " if ok else "FAIL ") + label + ((": " + detail) if detail and not ok else ""))
    if not ok:
        FAILS.append(label)


def project(root: Path, *, hook: bool) -> None:
    for d in (".nift", "content", "templates", "public", "scripts", "data"):
        (root / d).mkdir(parents=True, exist_ok=True)
    config = {"config": {
        "content-dir": "content/", "content-ext": ".html", "output-dir": "public/",
        "output-ext": ".html", "default-template": "templates/t.html", "build-threads": 1,
        "incremental-mode": "modified", "minify-exts": [],
    }}
    tracked = {"name": "index", "title": "Index"}
    if hook:
        config["config"]["pre build"] = "scripts/pre.f"
        tracked["pre-build"] = "scripts/pre.f"
    (root / ".nift/config.json").write_text(json.dumps(config))
    (root / ".nift/tracked.json").write_text(json.dumps({"tracked": [tracked]}))
    (root / "templates/t.html").write_text("@content")
    (root / "data/inside.txt").write_text("PAGE-OK")
    # `try` is refused by the VM compiler. If a page ever runs on the VM under
    # NIFT_VM=require, the build fails; on legacy it succeeds.
    (root / "content/index.html").write_text(
        '<p>@script { x := ""; try { x = open("data/inside.txt") } catch(e) { x = "ERR" }; return x }</p>\n')
    if hook:
        (root / "scripts/pre.f").write_text(
            'try { touch("hook-marker.txt") } catch(e) { }\n')


def run(nift: Path, root: Path, stats: Path, *args: str):
    env = dict(os.environ)
    env["NIFT_VM"] = "require"
    env["NIFT_VM_STATS"] = str(stats)
    env.pop("NIFT_PREVALIDATE", None)
    return subprocess.run([str(nift), *args], cwd=root, env=env, text=True, capture_output=True, timeout=120)


def vm_stats(path: Path):
    if not path.exists():
        return None
    rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    return rows[-1] if rows else None


def protected(label: str, stats: Path, executed: bool):
    """The tripwire predicate: the protected path executed and no VM execution
    record exists. Returns (ok, detail)."""
    record = vm_stats(stats)
    vm_ran = bool(record and (record.get("vm_executions", 0) or record.get("outcome") in ("vm", "vm-error")))
    ok = executed and not vm_ran
    detail = "" if ok else f"executed={executed} stats={record}"
    return ok, detail


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    args = ap.parse_args()
    nift = Path(args.nift).resolve()

    with tempfile.TemporaryDirectory(prefix="cp17-tripwire-") as work:
        work = Path(work)

        # Positive control: a standalone script really selects the VM.
        standalone = work / "standalone.f"
        standalone.write_text('print("VM-OK")\n')
        stats = work / "standalone.json"
        env = dict(os.environ)
        env["NIFT_VM"] = "require"
        env["NIFT_VM_STATS"] = str(stats)
        proc = subprocess.run([str(nift), str(standalone)], cwd=work, env=env, text=True, capture_output=True,
                              timeout=60)
        record = vm_stats(stats)
        check(proc.returncode == 0 and record and record.get("outcome") == "vm" and
              record.get("vm_executions") == 1 and "VM-OK" in proc.stdout,
              "standalone selects the VM", f"rc={proc.returncode} stats={record}")

        # Page and hook paths: legacy only.
        for label, hook in (("build/page", True), ("page-only", False), ("hook", True)):
            root = work / label.replace("/", "-")
            project(root, hook=hook)
            stats = root / "stats.json"
            proc = run(nift, root, stats, "build", "--all")
            page = root / "public/index.html"
            page_ok = proc.returncode == 0 and page.exists() and "PAGE-OK" in page.read_text(errors="replace")
            hook_ok = (root / "hook-marker.txt").exists() if hook else True
            ok, detail = protected(label, stats, page_ok and hook_ok)
            check(ok, f"{label}: legacy execution, no VM record", detail)

        # Detector self-test: a synthetic VM record must fail the predicate,
        # and a missing record without execution must fail too.
        fake = work / "fake.json"
        fake.write_text(json.dumps({"outcome": "vm", "vm_executions": 1}) + "\n")
        ok, _ = protected("negative: forced VM record", fake, True)
        check(not ok, "detector rejects a forced VM record")
        ok, _ = protected("negative: no execution", work / "none.json", False)
        check(not ok, "detector rejects missing stats without execution")

    print("PASS build/page/hook VM routing tripwire" if not FAILS else
          f"FAIL build/page/hook VM routing tripwire ({len(FAILS)} failures)")
    return 0 if not FAILS else 1


if __name__ == "__main__":
    sys.exit(main())
