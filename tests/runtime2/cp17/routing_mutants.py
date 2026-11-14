#!/usr/bin/env python3
"""R1-E: an isolated source mutant routes the real hook/build script boundary
through the VM selector, and the tripwire rejects the resulting execution."""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

PATCH = Path(__file__).resolve().parent / "routing-mutants" / "hook.patch"
PAGE_PATCH = Path(__file__).resolve().parent / "routing-mutants" / "page.patch"


def project(root: Path, kind: str):
    for d in (".nift", "content", "templates", "public", "scripts", "data"):
        (root / d).mkdir(parents=True, exist_ok=True)
    config = {"config": {
        "content-dir": "content/", "content-ext": ".html", "output-dir": "public/",
        "output-ext": ".html", "default-template": "templates/t.html", "build-threads": 1,
        "incremental-mode": "modified", "minify-exts": [],
    }}
    tracked = {"name": "index", "title": "Index"}
    if kind == "hook":
        config["config"]["pre build"] = "scripts/pre.f"
        tracked["pre-build"] = "scripts/pre.f"
        (root / "scripts/pre.f").write_text('native::touch("hook-marker.txt")\n')
        (root / "templates/t.html").write_text("@content")
        (root / "content/index.html").write_text("PAGE-OK\n")
    elif kind == "build":
        tracked["build"] = "scripts/build.f"
        (root / "scripts/build.f").write_text(
            'native::touch("public/index.html")\nnative::touch("build-marker.txt")\n')
        (root / "templates/t.html").write_text("@content")
        (root / "content/index.html").write_text("PAGE-OK\n")
    elif kind == "page":
        (root / "templates/t.html").write_text("@content")
        (root / "content/index.html").write_text('@script { native::touch("page-marker.txt") }\n')
    elif kind == "template":
        tracked["template"] = "templates/t.html"
        (root / "templates/t.html").write_text('@script { native::touch("template-marker.txt") }\n@content')
        (root / "content/index.html").write_text("PAGE-OK\n")
    (root / ".nift/config.json").write_text(json.dumps(config))
    (root / ".nift/tracked.json").write_text(json.dumps({"tracked": [tracked]}))


def run(nift: Path, root: Path):
    stats = root / "stats.jsonl"
    env = dict(os.environ, NIFT_VM="require", NIFT_VM_STATS=str(stats),
               NIFT_CP17_ROUTING_MUTANT="1")
    env.pop("NIFT_PREVALIDATE", None)
    proc = subprocess.run([str(nift), "build", "--all"], cwd=root, env=env,
                          capture_output=True, text=True, timeout=180)
    rows = [json.loads(x) for x in stats.read_text().splitlines() if x.strip()] if stats.exists() else []
    return proc, rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    args = ap.parse_args()
    source_root = Path(__file__).resolve().parents[3]
    failures = 0
    with tempfile.TemporaryDirectory(prefix="cp17-routing-mutant-") as tmp:
        root = Path(tmp)
        # B15 (combined-b3-b5.md section 75): the two patched units are compiled with nift's own
        # compile line into .build/mutant-nift (tests/runtime2/sanctioned_mutant.py) and linked with
        # nift's other objects; not `make nift` in an archive of HEAD. A patch that no longer applies
        # to the current source is a harness failure (a stale mutant), never a detection.
        mutant_dir = source_root / ".build" / "mutant-nift" / "cp17-routing"
        built = subprocess.run([sys.executable, "-B", str(source_root / "tests/runtime2/sanctioned_mutant.py"),
                                "--out", str(mutant_dir), "--patch", str(PATCH), "--patch", str(PAGE_PATCH)],
                               capture_output=True, text=True)
        if built.returncode != 0:
            print("FAIL routing mutant build: " + built.stdout + built.stderr)
            return 1
        print(built.stdout.strip().splitlines()[-1])
        (root / "mutant").mkdir()
        shutil.copy2(mutant_dir / "nift", root / "mutant" / "nift")
        shutil.rmtree(mutant_dir)
        for kind in ("hook", "build", "page", "template"):
            for label, binary in (("normal", Path(args.nift).resolve()), ("mutant", root / "mutant" / "nift")):
                proj = root / (kind + "-" + label)
                project(proj, kind)
                proc, rows = run(binary, proj)
                marker = (proj / f"{kind}-marker.txt").exists()
                page = (proj / "public/index.html").exists()
                vm = any(r.get("vm_executions", 0) or r.get("outcome") in ("vm", "vm-error") for r in rows)
                print(f"{kind}/{label}: exit={proc.returncode} marker={marker} page={page} vm_rows={len(rows)}")
                if proc.returncode != 0 or not marker or not page:
                    print(f"FAIL {kind}/{label}: protected execution did not produce output/marker")
                    failures += 1
                elif label == "normal" and vm:
                    print(f"FAIL {kind}: normal path executed on the VM")
                    failures += 1
                elif label == "mutant" and not vm:
                    print(f"FAIL {kind}: mutant did not reach the VM selector")
                    failures += 1
    print("PASS routing mutant (hook/build boundary): real VM execution rejected by the tripwire"
          if not failures else f"FAIL routing mutant: {failures} failure(s)")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
