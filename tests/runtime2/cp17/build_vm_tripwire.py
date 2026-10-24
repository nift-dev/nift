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


def project(root: Path, *, hook: bool, template: bool = False) -> None:
    for d in (".nift", "content", "templates", "public", "scripts", "data"):
        (root / d).mkdir(parents=True, exist_ok=True)
    config = {"config": {
        "content-dir": "content/", "content-ext": ".html", "output-dir": "public/",
        "output-ext": ".html", "default-template": "templates/t.html", "build-threads": 1,
        "incremental-mode": "modified", "minify-exts": [],
    }}
    tracked = {"name": "index", "title": "Index"}
    if template:
        tracked["template"] = "templates/t.html"
    if hook:
        config["config"]["pre build"] = "scripts/pre.f"
        tracked["pre-build"] = "scripts/pre.f"
    (root / ".nift/config.json").write_text(json.dumps(config))
    (root / ".nift/tracked.json").write_text(json.dumps({"tracked": [tracked]}))
    (root / "templates/t.html").write_text(
        'TPL:$[open("data/inside.txt")]\n@content\n' if template else "@content")
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
        return []
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]


def protected(label: str, stats: Path, executed: bool):
    """The tripwire predicate: the protected path executed, wrote statistics,
    and no record in the file shows a VM execution. Every row is inspected:
    a VM row followed by a fallback row is still a violation."""
    rows = vm_stats(stats)
    vm_ran = any(row.get("vm_executions", 0) or row.get("outcome") in ("vm", "vm-error") for row in rows)
    ok = executed and not vm_ran
    detail = "" if ok else f"executed={executed} stats={rows}"
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
        rows = vm_stats(stats)
        record = rows[-1] if rows else None
        check(proc.returncode == 0 and record and record.get("outcome") == "vm" and
              record.get("vm_executions") == 1 and "VM-OK" in proc.stdout,
              "standalone selects the VM", f"rc={proc.returncode} stats={rows}")

        # Page and hook paths: legacy only.
        for label, hook, template in (("build/page", True, False), ("page-only", False, False),
                                      ("hook", True, False), ("template", False, True)):
            root = work / label.replace("/", "-")
            project(root, hook=hook, template=template)
            stats = root / "stats.json"
            proc = run(nift, root, stats, "build", "--all")
            page = root / "public/index.html"
            text = page.read_text(errors="replace") if page.exists() else ""
            page_ok = proc.returncode == 0 and "PAGE-OK" in text and (not template or "TPL:PAGE-OK" in text)
            hook_ok = (root / "hook-marker.txt").exists() if hook else True
            ok, detail = protected(label, stats, page_ok and hook_ok)
            check(ok, f"{label}: legacy execution, no VM record", detail)

        # Missing statistics alone are not evidence of legacy execution.
        ok, _ = protected("negative: missing statistics", work / "none.json", False)
        check(not ok, "detector rejects missing statistics without execution")

        # Detector self-test: a synthetic VM record and a VM row followed by a
        # fallback row must both fail; a standalone real VM run provides the
        # same shape without synthetic statistics.
        fake = work / "fake.json"
        fake.write_text(json.dumps({"outcome": "vm", "vm_executions": 1}) + "\n")
        ok, _ = protected("negative: forced VM record", fake, True)
        check(not ok, "detector rejects a forced VM record")
        mixed = work / "mixed.json"
        mixed.write_text(json.dumps({"outcome": "vm", "vm_executions": 1}) + "\n" +
                         json.dumps({"outcome": "fallback", "vm_executions": 0}) + "\n")
        ok, _ = protected("negative: earlier VM record", mixed, True)
        check(not ok, "detector inspects earlier VM records")

        standalone_stats = work / "standalone-real.json"
        env = dict(os.environ)
        env["NIFT_VM"] = "require"
        env["NIFT_VM_STATS"] = str(standalone_stats)
        proc = subprocess.run([str(nift), str(standalone)], cwd=work, env=env, text=True,
                              capture_output=True, timeout=60)
        for label in ("build", "page", "hook", "template"):
            ok, _ = protected(f"negative: real VM record for {label}", standalone_stats, True)
            check(not ok and proc.returncode == 0, f"detector rejects a real VM record ({label})")

    print("PASS build/page/hook VM routing tripwire" if not FAILS else
          f"FAIL build/page/hook VM routing tripwire ({len(FAILS)} failures)")
    return 0 if not FAILS else 1


if __name__ == "__main__":
    sys.exit(main())
