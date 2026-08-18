#!/usr/bin/env python3
"""Smoke-test an installed/public Nift distribution channel.

This deliberately tests the executable as a user receives it rather than the
source checkout. It emits normalized JSON evidence suitable for Actions
artifacts and fails strictly when the installed version is not the requested
release.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import re
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--channel", required=True)
    parser.add_argument("--expected-version", required=True)
    parser.add_argument("--evidence", required=True, type=Path)
    parser.add_argument(
        "--command",
        action="append",
        required=True,
        help="One launcher argument; repeat for wrappers such as flatpak run.",
    )
    return parser.parse_args()


def run(command: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=60,
    )


def require_ok(result: subprocess.CompletedProcess[str], description: str) -> str:
    output = result.stdout.strip()
    if result.returncode != 0:
        raise RuntimeError(
            f"{description} failed with exit code {result.returncode}:\n{output}"
        )
    return output


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    args = parse_args()
    launcher = list(args.command)
    evidence: dict[str, object] = {
        "schema": 1,
        "channel": args.channel,
        "expected_version": args.expected_version,
        "launcher": launcher,
        "runner": {
            "system": platform.system(),
            "machine": platform.machine(),
        },
        "checked_at_utc": datetime.now(timezone.utc).isoformat(),
        "status": "failed",
        "checks": {},
    }

    args.evidence.parent.mkdir(parents=True, exist_ok=True)

    try:
        version_output = require_ok(run(launcher + ["version"]), "nift version")
        match = re.search(r"^Nift v(\d+\.\d+\.\d+)(?:\s|$)", version_output)
        if not match:
            raise RuntimeError(f"could not parse Nift version from: {version_output!r}")
        reported_version = match.group(1)
        if reported_version != args.expected_version:
            raise RuntimeError(
                f"distribution version mismatch: expected {args.expected_version}, "
                f"got {reported_version}"
            )

        about_output = require_ok(run(launcher + ["about"]), "nift about")
        commands_output = require_ok(run(launcher + ["commands"]), "nift commands")
        if "init [--target=platform] [--ext=.ext]" not in commands_output:
            raise RuntimeError("nift commands does not expose the v4.0.2 init contract")

        checks: dict[str, object] = {
            "version": reported_version,
            "about": "ok",
            "commands": "ok",
        }

        with tempfile.TemporaryDirectory(prefix="nift-distribution-") as temp:
            temp_root = Path(temp)

            basic = temp_root / "basic"
            basic.mkdir()
            require_ok(run(launcher + ["init"], basic), "nift init")
            require_ok(run(launcher + ["build"], basic), "basic nift build")
            basic_output = basic / "public" / "index.html"
            if not basic_output.is_file():
                raise RuntimeError("basic init/build did not create public/index.html")
            checks["basic_site"] = {
                "output": "public/index.html",
                "sha256": sha256(basic_output),
            }

            vercel = temp_root / "vercel"
            vercel.mkdir()
            require_ok(
                run(launcher + ["init", "--target=vercel"], vercel),
                "nift init --target=vercel",
            )
            require_ok(run(launcher + ["build"], vercel), "Vercel-target nift build")
            vercel_config = vercel / ".vercel" / "output" / "config.json"
            vercel_output = vercel / ".vercel" / "output" / "static" / "index.html"
            if not vercel_config.is_file() or not vercel_output.is_file():
                raise RuntimeError("Vercel target did not create the documented output contract")
            config = json.loads(vercel_config.read_text(encoding="utf-8"))
            if config.get("version") != 3:
                raise RuntimeError("Vercel target config.json does not declare version 3")
            checks["vercel_target"] = {
                "config_version": 3,
                "output": ".vercel/output/static/index.html",
                "sha256": sha256(vercel_output),
            }

        evidence["checks"] = checks
        evidence["status"] = "passed"
        evidence["about_first_line"] = about_output.splitlines()[0] if about_output else ""
        print(
            f"distribution smoke passed: {args.channel} -> Nift {reported_version} "
            f"on {platform.system()} {platform.machine()}"
        )
        return 0
    except Exception as exc:
        evidence["error"] = str(exc)
        print(f"distribution smoke failed: {exc}")
        return 1
    finally:
        args.evidence.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
