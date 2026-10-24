"""Verify that the executable under test is the intended mutant build.

The Makefile writes a manifest next to each mutant binary after compiling it.
The check requires a native executable, the mutant id, the build recipe name,
the unmutated baseline SHA256 and the executable SHA256. This rejects wrapper
scripts (such as the review's heap-replay control) even when they supply a
manifest matching their own bytes.
"""
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


def _sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def verify(path, mutant, manifest, normal, build):
    if not manifest or not os.path.exists(manifest):
        return None, "mutant provenance manifest is required"
    repo = Path(__file__).resolve().parents[2]
    directory = "closure-mutants" if build == "closure-mutant" else "for-mutants"
    trusted_bin = repo / ".build" / directory / f"nift-{mutant}"
    trusted_manifest = repo / ".build" / directory / f"nift-{mutant}.json"
    try:
        if Path(path).resolve() != trusted_bin.resolve():
            return None, f"executable is outside the reviewed build output {trusted_bin}"
        if Path(manifest).resolve() != trusted_manifest.resolve():
            return None, f"manifest is outside the reviewed build output {trusted_manifest}"
    except OSError as e:
        return None, f"cannot resolve build provenance paths: {e}"
    try:
        tree = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD^{tree}"],
                                       text=True, stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError):
        return None, "cannot determine the reviewed source tree"
    try:
        with open(path, "rb") as f:
            if f.read(4) != b"\x7fELF":
                return None, "mutant executable is not a native ELF build"
    except OSError as e:
        return None, f"cannot read mutant executable: {e}"
    try:
        data = json.load(open(manifest))
    except (OSError, ValueError) as e:
        return None, f"invalid mutant provenance manifest: {e}"
    if data.get("mutant") != mutant:
        return None, f"manifest names mutant {data.get('mutant')!r}, expected {mutant}"
    if data.get("build") != build:
        return None, f"manifest build {data.get('build')!r}, expected {build}"
    if data.get("source_tree") != tree:
        return None, f"manifest source tree {data.get('source_tree')!r} != reviewed tree {tree}"
    expected = data.get("sha256")
    if not isinstance(expected, str) or len(expected) != 64:
        return None, "manifest has no valid sha256"
    if normal is None:
        return None, "unmutated baseline is required for provenance"
    try:
        normal_sha = _sha256(normal)
        actual = _sha256(path)
    except OSError as e:
        return None, f"cannot hash build provenance: {e}"
    if data.get("normal_sha256") != normal_sha:
        return None, "manifest does not bind the supplied unmutated baseline"
    if actual != expected:
        return None, f"mutant executable SHA256 {actual} != manifest {expected}"
    # Independently rebuild the intended mutant through the reviewed Makefile
    # recipe in a fresh output directory and bind credit to that build result.
    temp = tempfile.mkdtemp(prefix="cp12c-mutant-build-")
    try:
        variant = "CLOSURE_MUTANT" if build == "closure-mutant" else "FOR_MUTANT"
        target = Path(temp) / directory / f"nift-{mutant}"
        built = subprocess.run(["make", "-s", "-C", str(repo), f"TEST_DIR={temp}",
                                f"{variant}={mutant}", str(target)],
                               capture_output=True, text=True, timeout=300)
        if built.returncode != 0 or not target.exists():
            return None, "trusted mutant rebuild failed"
        trusted_sha = _sha256(target)
    except (OSError, subprocess.SubprocessError) as e:
        return None, f"trusted mutant rebuild failed: {e}"
    finally:
        shutil.rmtree(temp, ignore_errors=True)
    if actual != trusted_sha:
        return None, f"executable SHA256 {actual} != trusted rebuild {trusted_sha}"
    return data, None
