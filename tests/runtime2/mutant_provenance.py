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


def _sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def verify(path, mutant, manifest, normal, build):
    if not manifest or not os.path.exists(manifest):
        return None, "mutant provenance manifest is required"
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
    return data, None
