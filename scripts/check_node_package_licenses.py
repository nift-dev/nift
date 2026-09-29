#!/usr/bin/env python3
import json
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
NODE = ROOT / "bindings" / "node"
PAIRS = (
    (ROOT / "THIRD_PARTY_NOTICES.md", NODE / "THIRD_PARTY_NOTICES.md"),
    (ROOT / "third_party" / "libffi" / "LICENSE", NODE / "LICENSE-libffi"),
)

for canonical, packaged in PAIRS:
    if not packaged.is_file():
        raise SystemExit(f"missing Node package legal file: {packaged.relative_to(ROOT)}")
    if canonical.read_bytes() != packaged.read_bytes():
        raise SystemExit(
            f"Node package legal file drift: {packaged.relative_to(ROOT)} != "
            f"{canonical.relative_to(ROOT)}"
        )

manifest = json.loads((NODE / "package.json").read_text(encoding="utf-8"))
required = {"THIRD_PARTY_NOTICES.md", "LICENSE-libffi"}
missing_manifest = required.difference(manifest.get("files", []))
if missing_manifest:
    raise SystemExit(f"Node package.json omits legal files: {sorted(missing_manifest)}")

result = subprocess.run(
    ["npm", "pack", "--dry-run", "--json"],
    cwd=NODE,
    check=True,
    capture_output=True,
    text=True,
)
payload = json.loads(result.stdout)
if len(payload) != 1:
    raise SystemExit("npm pack --dry-run returned an unexpected result")
packed = {entry["path"] for entry in payload[0].get("files", [])}
missing_payload = required.difference(packed)
if missing_payload:
    raise SystemExit(f"ordinary npm pack omits legal files: {sorted(missing_payload)}")

print("Node package legal payload and drift check: PASS")
