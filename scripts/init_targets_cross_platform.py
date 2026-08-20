#!/usr/bin/env python3
"""Portable smoke coverage for Nift init/target behavior."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

TARGETS = (
    "vercel",
    "netlify",
    "amplify",
    "azure",
    "firebase",
    "render",
    "cloudflare",
    "github-pages",
)


def run(nift: Path, cwd: Path, *args: str, expect: int = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(nift), *args],
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != expect:
        raise AssertionError(
            f"{nift.name} {' '.join(args)} returned {result.returncode}, expected {expect}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result




def nift_version(nift: Path) -> str:
    result = run(nift, nift.parent, "--version")
    text = result.stdout.strip()
    prefix = "Nift v"
    if not text.startswith(prefix) or len(text) == len(prefix):
        raise AssertionError(f"unexpected Nift version output: {text!r}")
    return text[len(prefix):]


def init(nift: Path, root: Path, name: str, *args: str) -> Path:
    project = root / name
    project.mkdir()
    run(nift, project, "init", *args)
    return project


def load_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def check(nift: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="nift-init-targets-") as td:
        root = Path(td)

        basic = init(nift, root, "basic")
        assert (basic / "content/index.html").is_file()
        assert (basic / "public/index.html").is_file()
        assert "<title>index</title>" in (basic / "public/index.html").read_text(encoding="utf-8")

        html = init(nift, root, "html", "--ext=.html")
        assert load_json(basic / ".nift/config.json") == load_json(html / ".nift/config.json")
        assert load_json(basic / ".nift/tracked.json") == load_json(html / ".nift/tracked.json")

        php = init(nift, root, "php", "--ext=.php")
        assert (php / "content/index.php").is_file()
        assert (php / "public/index.php").is_file()
        assert "<title>index</title>" in (php / "public/index.php").read_text(encoding="utf-8")
        assert (php / "content/assets/css/style.css").is_file()

        text = init(nift, root, "text", "--ext=.txt")
        cfg = load_json(text / ".nift/config.json")["config"]
        assert cfg["default-template"] == ""
        assert cfg["content-ext"] == ".txt"
        assert cfg["output-ext"] == ".txt"
        assert not (text / "content/assets").exists()

        projects: dict[str, Path] = {}
        for target in TARGETS:
            project = init(nift, root, target, f"--target={target}")
            run(nift, project, "build")
            projects[target] = project

        vercel = projects["vercel"]
        assert load_json(vercel / ".vercel/output/config.json") == {"version": 3}
        assert load_json(vercel / ".nift/config.json")["config"]["output-dir"] == ".vercel/output/static/"
        assert (vercel / ".vercel/output/static/index.html").is_file()

        amplify = projects["amplify"]
        manifest = load_json(amplify / ".amplify-hosting/deploy-manifest.json")
        assert manifest["version"] == 1
        assert manifest["routes"] == [{"path": "/*", "target": {"kind": "Static"}}]
        assert manifest["framework"]["name"] == "nift"
        assert manifest["framework"]["version"] == nift_version(nift)
        assert (amplify / ".amplify-hosting/static/index.html").is_file()

        assert 'command = "nift build"' in (projects["netlify"] / "netlify.toml").read_text(encoding="utf-8")
        assert 'publish = "public"' in (projects["netlify"] / "netlify.toml").read_text(encoding="utf-8")
        assert (projects["azure"] / "public/staticwebapp.config.json").is_file()
        assert load_json(projects["firebase"] / "firebase.json")["hosting"]["public"] == "public"
        assert "runtime: static" in (projects["render"] / "render.yaml").read_text(encoding="utf-8")
        assert "staticPublishPath: ./public" in (projects["render"] / "render.yaml").read_text(encoding="utf-8")
        assert 'pages_build_output_dir = "./public"' in (projects["cloudflare"] / "wrangler.toml").read_text(encoding="utf-8")
        assert (projects["github-pages"] / "public/index.html").is_file()
        assert not (projects["github-pages"] / ".github/workflows/pages.yml").exists()

        existing = root / "vercel-existing-ignore"
        existing.mkdir()
        (existing / ".gitignore").write_text("node_modules/\n", encoding="utf-8")
        run(nift, existing, "init", "--target=vercel")
        lines = (existing / ".gitignore").read_text(encoding="utf-8").splitlines()
        assert "node_modules/" in lines
        assert ".vercel/output/static/" in lines

        failures = [
            ((".html",), "positional init arguments are no longer supported"),
            (("--target=does-not-exist",), "unknown init target 'does-not-exist'"),
            (("--target=vercel", "--ext=.php"), "extension '.php' is not supported by target 'vercel'"),
            (("--ext", ".php"), "--ext requires '=EXT'"),
            (("--target", "vercel"), "--target requires '=PLATFORM'"),
        ]
        for idx, (args, message) in enumerate(failures):
            project = root / f"failure-{idx}"
            project.mkdir()
            result = run(nift, project, "init", *args, expect=1)
            assert message in result.stderr

        removed = root / "init-html"
        removed.mkdir()
        result = run(nift, removed, "init-html", expect=1)
        assert "command 'init-html' has been removed" in result.stderr
        assert "use 'nift init' instead" in result.stderr


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nift", required=True, type=Path)
    args = parser.parse_args()
    nift = args.nift.resolve()
    if os.name == "nt" and nift.suffix.lower() != ".exe" and not nift.exists():
        exe = nift.with_suffix(".exe")
        if exe.exists():
            nift = exe
    if not nift.is_file():
        raise SystemExit(f"Nift executable not found: {nift}")
    check(nift)
    print("cross-platform init target smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
