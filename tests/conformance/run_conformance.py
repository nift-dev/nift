#!/usr/bin/env python3
"""Portable project-semantics conformance corpus driver (PA5).

Runs every case under tests/conformance/cases/ against BOTH the Nift CLI and
the C++ project-aware Engine and checks the agreed observable contracts:

  parity cases:  `nift build-all` output for a tracked page must be byte-
                 identical to Engine.render(page) output, and (when the case
                 sets "deps_reqs": true) the CLI .info.json dependencies/reqs
                 sets must equal Engine result.dependencies()/requirements().
  reject cases:  both implementations must reject the same invalid project or
                 source state (CLI build fails; Engine render fails).

The cases are fixture projects + expected.json manifests, so they are portable:
nift-rs inherits the same corpus later. Engine-only embedding contracts
(Context overlays, injected environment provider, unknown page, is_open/
open_error) and C++ Engine lifecycle contracts (atomic metadata-generation
reload, last-good retention) have no artificial CLI equivalent and are covered
by tests/engine_project.cpp and tests/engine_reload.cpp respectively; the
manifest classifies all of this explicitly.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
CASES = Path(__file__).resolve().parent / "cases"
NIFT = os.environ.get("NIFT_BIN", str(REPO / "nift"))
RUNNER = os.environ.get("CPP_RUNNER", str(REPO / ".build" / "cpp-runner"))


def esc(page):
    if page == "/":
        return "ROOT"
    return page.replace("/", "_")


def run(cmd, cwd, extra_env=None):
    env = dict(os.environ)
    if extra_env:
        env.update(extra_env)
    return subprocess.run(cmd, cwd=str(cwd), env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def cli_deps_reqs(project, output_rel):
    rel = str(Path(output_rel))
    for ext in (".html", ".xml", ".css"):
        if rel.endswith(ext):
            rel = rel[: -len(ext)]
            break
    else:
        if rel.endswith(".js"):
            rel = rel[:-3]
    path = Path(project) / ".nift" / (rel + ".info.json")
    if not path.exists():
        return None
    doc = json.loads(path.read_text())
    return sorted(doc.get("dependencies", [])), sorted(doc.get("reqs", []))


def run_parity(name, case, project, extra_env):
    cli = run([NIFT, "build-all"], project, extra_env)
    if cli.returncode != 0:
        return f"{name}: CLI build failed: {cli.stderr[:400]}"
    pages = list(case["pages"])
    eng = run([RUNNER, str(project), str(project.parent), *pages], project, extra_env)
    if eng.returncode != 0:
        return f"{name}: cpp_runner failed: {eng.stderr[:400]}"
    for page, output_rel in case["pages"].items():
        base = esc(page)
        engine_out = (project.parent / (base + ".out")).read_text()
        cli_out = (project / output_rel).read_text()
        if engine_out != cli_out:
            return f"{name}: page '{page}' output differs from CLI"
        if case.get("deps_reqs"):
            info = cli_deps_reqs(project, output_rel)
            if info is None:
                return f"{name}: page '{page}' CLI info.json missing"
            cli_deps, cli_reqs = info
            eng_deps = sorted((project.parent / (base + ".deps")).read_text().splitlines())
            eng_reqs = sorted((project.parent / (base + ".reqs")).read_text().splitlines())
            if cli_deps != eng_deps:
                return f"{name}: page '{page}' dependencies differ\n  CLI: {cli_deps}\n  Eng: {eng_deps}"
            if cli_reqs != eng_reqs:
                return f"{name}: page '{page}' requirements differ\n  CLI: {cli_reqs}\n  Eng: {eng_reqs}"
    return None


def run_reject(name, case, project, extra_env):
    cli = run([NIFT, "build-all"], project, extra_env)
    if cli.returncode == 0:
        return f"{name}: CLI unexpectedly succeeded"
    pages = list(case["pages"])
    eng = run([RUNNER, str(project), str(project.parent), *pages], project, extra_env)
    if eng.returncode != 0:
        return f"{name}: cpp_runner failed"
    for page in pages:
        base = esc(page)
        engine_out = (project.parent / (base + ".out")).read_text()
        if not engine_out.startswith("ERROR: "):
            return f"{name}: page '{page}' unexpectedly rendered: {engine_out[:120]}"
    return None


def main():
    failures = []
    ran = 0
    for case_dir in sorted(CASES.iterdir()):
        if not case_dir.is_dir():
            continue
        meta_path = case_dir / "expected.json"
        if not meta_path.exists():
            continue
        meta = json.loads(meta_path.read_text())
        name = case_dir.name
        with tempfile.TemporaryDirectory(prefix="nift-conformance-") as td:
            project = Path(td) / "project"
            shutil.copytree(case_dir / "project", project)
            extra_env = meta.get("env")
            if meta["category"] == "parity":
                err = run_parity(name, meta, project, extra_env)
            elif meta["category"] == "reject":
                err = run_reject(name, meta, project, extra_env)
            else:
                err = f"{name}: unknown category {meta['category']}"
            ran += 1
            if err:
                failures.append(err)
                print("FAIL " + name)
                print("     " + err.replace("\n", "\n     "))
            else:
                print("PASS " + name)
    print(f"\nconformance: {ran - len(failures)}/{ran} cases passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
