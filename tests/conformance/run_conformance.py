#!/usr/bin/env python3
"""Portable project-semantics conformance corpus driver (PA5).

Runs every case under tests/conformance/cases/ against BOTH the Nift CLI and
the C++ project-aware Engine and checks the agreed observable contracts:

  parity cases:  each page has a committed canonical golden under
                 cases/<name>/expected/<page>.out/.deps/.reqs (generated from
                 the CLI by gen_golden.py). The driver requires:
                   CLI output   == canonical golden
                   Engine output == canonical golden
                   CLI output   == Engine output   (differential, kept)
                 and the same triple equality for dependency and requirement
                 sets (golden vs CLI .info.json vs Engine result).
  reject cases:  both implementations must reject the same invalid project or
                 source state (CLI build fails; Engine render fails) AND the
                 failure must be for the intended semantic reason, validated by
                 a stable diagnostic substring per REJECT_CLASSES. Diagnostic
                 prose is not part of the public contract; only the semantic
                 class is.

The cases are fixture projects + expected.json manifests, so they are portable:
nift-rs inherits the same corpus later. Engine-only embedding contracts
(Context overlays, injected environment provider, unknown page, is_open/
open_error) and C++ Engine lifecycle contracts (atomic metadata-generation
reload, last-good retention) have no artificial CLI equivalent and are covered
by tests/engine_project.cpp and tests/engine_reload.cpp respectively; the
manifest classifies all of this explicitly.
"""

# Semantic rejection classes: the reason an invalid fixture must be rejected.
# Each class lists stable diagnostic substrings accepted from either
# implementation (CLI stderr or Engine render error); matching prose is not
# itself a public contract.
REJECT_CLASSES = {
    "missing-source": ["content file"],
    "unknown-config-key": ["unknown config key"],
    "duplicate-tracked-name": ["duplicate tracked name"],
    "invalid-tracking-json": ["invalid tracked.json"],
    "project-root-escape": ["path must stay inside the Nift project"],
}
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


def _lines(path):
    return sorted(path.read_text().splitlines())


def run_parity(name, case, case_dir, project, extra_env):
    cli = run([NIFT, "build-all"], project, extra_env)
    if cli.returncode != 0:
        return f"{name}: CLI build failed: {cli.stderr[:400]}"
    pages = list(case["pages"])
    eng = run([RUNNER, str(project), str(project.parent), *pages], project, extra_env)
    if eng.returncode != 0:
        return f"{name}: cpp_runner failed: {eng.stderr[:400]}"
    expected_dir = case_dir / "expected"
    for page, output_rel in case["pages"].items():
        base = esc(page)
        golden_out = (expected_dir / (base + ".out")).read_text()
        engine_out = (project.parent / (base + ".out")).read_text()
        cli_out = (project / output_rel).read_text()
        # Canonical oracle: both implementations must reproduce the golden
        # output, and (kept for differential value) agree with each other.
        if cli_out != golden_out:
            return f"{name}: page '{page}' CLI output differs from canonical golden"
        if engine_out != golden_out:
            return f"{name}: page '{page}' Engine output differs from canonical golden"
        if engine_out != cli_out:
            return f"{name}: page '{page}' CLI and Engine outputs differ"
        # Dependency/requirement sets: golden == CLI .info.json == Engine result.
        golden_deps = _lines(expected_dir / (base + ".deps"))
        golden_reqs = _lines(expected_dir / (base + ".reqs"))
        info = cli_deps_reqs(project, output_rel)
        if info is None:
            return f"{name}: page '{page}' CLI info.json missing"
        cli_deps, cli_reqs = info
        eng_deps = _lines(project.parent / (base + ".deps"))
        eng_reqs = _lines(project.parent / (base + ".reqs"))
        if golden_deps != cli_deps or golden_deps != eng_deps:
            return (f"{name}: page '{page}' dependencies differ\n"
                    f"  golden: {golden_deps}\n  CLI: {cli_deps}\n  Eng: {eng_deps}")
        if golden_reqs != cli_reqs or golden_reqs != eng_reqs:
            return (f"{name}: page '{page}' requirements differ\n"
                    f"  golden: {golden_reqs}\n  CLI: {cli_reqs}\n  Eng: {eng_reqs}")
    return None


def _matches_class(text, klass):
    needles = REJECT_CLASSES.get(klass)
    if not needles:
        return False
    return any(needle in text for needle in needles)


def run_reject(name, case, project, extra_env):
    klass = case.get("expect")
    if not klass:
        return f"{name}: reject case missing semantic 'expect' class"
    cli = run([NIFT, "build-all"], project, extra_env)
    if cli.returncode == 0:
        return f"{name}: CLI unexpectedly succeeded"
    if not _matches_class(cli.stderr, klass):
        return f"{name}: CLI rejected for the wrong semantic class (expected '{klass}')"
    pages = list(case["pages"])
    eng = run([RUNNER, str(project), str(project.parent), *pages], project, extra_env)
    if eng.returncode != 0:
        return f"{name}: cpp_runner failed"
    for page in pages:
        base = esc(page)
        engine_out = (project.parent / (base + ".out")).read_text()
        if not engine_out.startswith("ERROR: "):
            return f"{name}: page '{page}' unexpectedly rendered: {engine_out[:120]}"
        if not _matches_class(engine_out, klass):
            return f"{name}: page '{page}' Engine rejected for the wrong semantic class (expected '{klass}')"
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
                err = run_parity(name, meta, case_dir, project, extra_env)
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
