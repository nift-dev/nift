"""Verify that the executable under test is the intended mutant build.

The Makefile writes a small manifest next to each mutant binary with the
mutant id and the binary SHA256. A fake executable cannot satisfy this check
unless it was deliberately substituted in the build tree as well.
"""
import hashlib
import json
import os


def verify(path, mutant, manifest):
    if not manifest or not os.path.exists(manifest):
        return None, "mutant provenance manifest is required"
    try:
        data = json.load(open(manifest))
    except (OSError, ValueError) as e:
        return None, f"invalid mutant provenance manifest: {e}"
    if data.get("mutant") != mutant:
        return None, f"manifest names mutant {data.get('mutant')!r}, expected {mutant}"
    expected = data.get("sha256")
    if not isinstance(expected, str) or len(expected) != 64:
        return None, "manifest has no valid sha256"
    h = hashlib.sha256()
    try:
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
    except OSError as e:
        return None, f"cannot hash mutant executable: {e}"
    actual = h.hexdigest()
    if actual != expected:
        return None, f"mutant executable SHA256 {actual} != manifest {expected}"
    return data, None
