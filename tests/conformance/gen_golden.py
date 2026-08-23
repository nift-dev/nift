#!/usr/bin/env python3
"""Regenerate canonical golden expectations for conformance parity cases.

For every parity case, builds the project with the Nift CLI (the reference
implementation) and records the rendered output and the dependency/requirement
sets for each listed page into cases/<name>/expected/<page>.out/.deps/.reqs.
These goldens are the implementation-independent oracle: the conformance driver
requires CLI output == golden, Engine output == golden, and CLI == Engine.

Regenerate only when a Nift semantic change is deliberate; never to make a
failing corpus pass.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from run_conformance import CASES, NIFT, cli_deps_reqs, esc

REPO = Path(__file__).resolve().parent.parent.parent


def main():
    regenerated = 0
    for case_dir in sorted(CASES.iterdir()):
        meta_path = case_dir / "expected.json"
        if not meta_path.exists():
            continue
        meta = json.loads(meta_path.read_text())
        if meta.get("category") != "parity":
            continue
        name = case_dir.name
        with tempfile.TemporaryDirectory(prefix="nift-golden-") as td:
            project = Path(td) / "project"
            shutil.copytree(case_dir / "project", project)
            env = dict(os.environ)
            env.update(meta.get("env") or {})
            cli = subprocess.run([NIFT, "build-all"], cwd=str(project), env=env,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if cli.returncode != 0:
                print(f"SKIP {name}: CLI build failed; no golden written")
                continue
            expected_dir = case_dir / "expected"
            expected_dir.mkdir(exist_ok=True)
            for stale in expected_dir.glob("*"):
                stale.unlink()
            for page, output_rel in meta["pages"].items():
                base = esc(page)
                shutil.copyfile(project / output_rel, expected_dir / f"{base}.out")
                info = cli_deps_reqs(project, output_rel)
                if info is None:
                    print(f"SKIP {name}: page '{page}' has no CLI info.json")
                    continue
                cli_deps, cli_reqs = info
                (expected_dir / f"{base}.deps").write_text("\n".join(cli_deps))
                (expected_dir / f"{base}.reqs").write_text("\n".join(cli_reqs))
            print(f"GOLDEN {name}")
            regenerated += 1
    print(f"\nregenerated {regenerated} parity case(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
