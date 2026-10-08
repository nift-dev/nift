#!/usr/bin/env python3
"""Reproduce probe semantics locally without changing the frozen benchmark repos.

Usage: python3 reproduce.py /absolute/path/to/nift [probe-name]
Run against separately built baseline and candidate binaries for fresh profiles.
Fixture paths change, so counts need not be byte-identical to recorded originals.
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent
binary = str(Path(sys.argv[1]).resolve())
selected = sys.argv[2] if len(sys.argv) > 2 else None
rows = json.loads((root / "probes.json").read_text())
fixtures = json.loads((root / "fixtures.json").read_text())
with tempfile.TemporaryDirectory(prefix="nift-cp410-repro-") as directory:
    work = Path(directory)
    replacements = {}
    for old, info in fixtures.items():
        if "tree" in info:
            tree = work / info["tree"].removesuffix(".json")
            for name in json.loads((root / "fixtures" / info["tree"]).read_text()):
                path = tree / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            replacements[old] = str(tree) + "/**/*.txt"
        else:
            replacements[old] = str(root / "fixtures" / info["file"])
    (work / "owner.f").write_text((root / "probes" / "owner.f").read_text())
    executed = 0
    for row in rows:
        if selected and row["name"] != selected:
            continue
        path = work / Path(row["path"]).name
        source = (root / "probes" / path.name).read_text()
        for old, new in replacements.items():
            source = source.replace(old, new)
        path.write_text(source)
        result = subprocess.run([binary, str(path)], text=True, capture_output=True,
                                timeout=120)
        if result.returncode or result.stdout.strip() != row["expected"]:
            raise SystemExit(f"FAIL {row['name']}/{row['n']}: {result.stdout} {result.stderr}")
        print(f"PASS {row['name']}/{row['n']}")
        executed += 1
    if not executed:
        raise SystemExit("No matching probe")
