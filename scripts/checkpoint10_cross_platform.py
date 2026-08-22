#!/usr/bin/env python3
"""Checkpoint 10 portable behavioural corpus.

The evidence intentionally separates runner metadata from comparable semantics.
Only presentation noise is normalized: path separators and CRLF line endings.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import platform
import shutil
import stat
import subprocess
import tempfile


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nift", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--runner-os", default=platform.system())
    return parser.parse_args()


ARGS = parse_args()
NIFT = str(pathlib.Path(ARGS.nift).resolve())
REPO = pathlib.Path(__file__).resolve().parents[1]


def git_commit():
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=REPO, text=True,
            stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def compiler_identity():
    for command in (["g++", "--version"], ["c++", "--version"]):
        try:
            output = subprocess.check_output(
                command, text=True, encoding="utf-8", errors="replace",
                stderr=subprocess.STDOUT)
            return output.splitlines()[0].strip()
        except (OSError, subprocess.CalledProcessError):
            pass
    return "unknown"


def run(root, *arguments, expect=0):
    result = subprocess.run(
        [NIFT, *arguments], cwd=root, text=True, encoding="utf-8", errors="strict",
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    if result.returncode != expect:
        raise RuntimeError(
            f"{arguments} returned {result.returncode}, expected {expect}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}")
    return result


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def write_json(path, value):
    write(path, json.dumps(value, ensure_ascii=False, separators=(",", ":")) + "\n")


def normalized_bytes(path):
    # Newline spelling is not a Nift contract in this corpus. No other output
    # content is normalized before hashing.
    return path.read_bytes().replace(b"\r\n", b"\n")


def output_tree(root):
    result = {}
    public = root / "public"
    for path in sorted(public.rglob("*")):
        if path.is_file():
            relative = path.relative_to(public).as_posix()
            result[relative] = hashlib.sha256(normalized_bytes(path)).hexdigest()
    return result


def normalize_paths(values):
    return sorted(str(value).replace("\\", "/") for value in values)


def selected_metadata(root):
    tracked = json.loads((root / ".nift/tracked.json").read_text(encoding="utf-8"))["tracked"]
    entries = []
    for item in tracked:
        entries.append({
            key: item[key] for key in
            ("name", "title", "template", "content-ext", "output-ext", "minify")
            if key in item
        })
    pages = {}
    metadata_root = root / ".nift/public"
    if metadata_root.exists():
        for path in sorted(metadata_root.rglob("*.info.json")):
            data = json.loads(path.read_text(encoding="utf-8"))
            relative = path.relative_to(metadata_root).as_posix()
            pages[relative] = {
                "template": str(data.get("template", "")).replace("\\", "/"),
                "dependencies": normalize_paths(data.get("dependencies", [])),
                "requirements": normalize_paths(data.get("reqs", [])),
                "minify": data.get("minify", False),
                "minify_version": data.get("minify-version", 0),
            }
    return {"tracked": entries, "pages": pages}


def status_class(root):
    result = run(root, "status")
    combined = result.stdout + result.stderr
    return "stale" if "needs rebuilding" in combined else "clean"


def diagnostic_class(result):
    text = result.stdout + result.stderr
    classes = (
        ("json-schema-validation", "does not satisfy schema"),
        ("malformed-template", "unterminated"),
        ("parse-depth", "maximum template parse depth"),
    )
    for name, marker in classes:
        if marker in text:
            return name
    return "controlled-render-error"


def observation(root, **extra):
    value = {
        "outputs": output_tree(root),
        "state": selected_metadata(root),
        "status": status_class(root),
    }
    value.update(extra)
    return value


def setup_project(root):
    config = {
        "config": {
            "content-dir": "content/",
            "content-ext": ".html",
            "output-dir": "public/",
            "output-ext": ".html",
            "default-template": "templates/page.html",
            "build-threads": 2,
            "incremental-mode": "hash",
            "minify-exts": [],
            "contracts": {"routes": ".nift/routes.json"},
        }
    }
    tracked = {
        "tracked": [
            {"name": "/", "title": "Home", "template": "templates/page.html"},
            {"name": "docs/index", "title": "Docs", "template": "templates/page.html"},
            {"name": "nested/深い/ページ", "title": "Unicode", "template": "templates/page.html"},
            {"name": "assets/style", "title": "Style", "content-ext": ".css",
             "output-ext": ".css", "minify": True},
        ]
    }
    write_json(root / ".nift/config.json", config)
    write_json(root / ".nift/tracked.json", tracked)
    write_json(root / ".nift/routes.json", {"home": "/", "docs": "/docs/"})
    write_json(root / "data/site.json", {"name": "Nift", "items": [{"value": 1}, {"value": 2}]})
    write_json(root / "schemas/site.schema.json", {
        "type": "object", "required": ["name", "items"],
        "properties": {"name": {"type": "string"}, "items": {"type": "array", "maxItems": 8}}
    })
    write(root / "templates/page.html",
          '@json("data/site.json", site, "schemas/site.schema.json")\n'
          '@input("parts/shared.html")\n'
          '<nav><a href="$[routes.home]">$[site.name]</a></nav>\n'
          '<a href="@pathto(\'docs/index\')">docs</a>\n'
          '@for(item : site.items){<i>$[item.value]</i>}\n@content\n')
    write(root / "templates/parts/shared.html", "<strong>shared-one</strong>\n")
    write(root / "content/index.html", "<main>home-one</main>\n")
    write(root / "content/docs/index.html", "<main>docs-one</main>\n")
    # Outputs preserve the source content file's permissions, so this source is
    # made read-only to keep its generated output read-only. That preserves the
    # portable read-only-output deletion distinction exercised below (POSIX
    # unlinks a read-only output from a writable directory; Windows requires
    # its write attribute to be cleared first).
    (root / "content/docs/index.html").chmod(stat.S_IREAD | stat.S_IRGRP | stat.S_IROTH)
    write(root / "content/nested/深い/ページ.html", "<p>Grüße 世界 🚀</p>\n")
    write(root / "content/assets/style.css", ".x { color : red ; margin : 0  1rem ; }\n")


def add_case(cases, name, root, **extra):
    cases.append({
        "name": name,
        "classification": "portable",
        "pass": True,
        "observation": observation(root, **extra),
    })


def main():
    cases = []
    with tempfile.TemporaryDirectory(prefix="nift-cp10-") as directory:
        root = pathlib.Path(directory) / "project"
        setup_project(root)

        run(root, "build-all")
        add_case(cases, "clean-build", root, exit_class="success")

        before = output_tree(root)
        run(root, "build-updated")
        add_case(cases, "no-op-incremental", root, exit_class="success",
                 output_unchanged=(before == output_tree(root)))

        write(root / "content/index.html", "<main>home-two</main>\n")
        stale = status_class(root)
        run(root, "build-updated")
        add_case(cases, "content-invalidation", root, stale_before=stale)

        page = (root / "templates/page.html").read_text(encoding="utf-8")
        write(root / "templates/page.html", "<header>template-two</header>\n" + page)
        stale = status_class(root)
        run(root, "build-updated")
        add_case(cases, "template-invalidation", root, stale_before=stale)

        write(root / "templates/parts/shared.html", "<strong>shared-two</strong>\n")
        stale = status_class(root)
        run(root, "build-updated")
        add_case(cases, "input-dependency-invalidation", root, stale_before=stale)

        write_json(root / "data/site.json", {"name": "Nift 4", "items": [{"value": 3}]})
        stale = status_class(root)
        run(root, "build-updated")
        add_case(cases, "json-invalidation", root, stale_before=stale)

        write_json(root / "schemas/site.schema.json", {
            "type": "object", "required": ["name", "items"],
            "properties": {"name": {"type": "string", "minLength": 1},
                           "items": {"type": "array", "maxItems": 4}}
        })
        stale = status_class(root)
        run(root, "build-updated")
        add_case(cases, "schema-invalidation", root, stale_before=stale)

        write_json(root / ".nift/routes.json", {"home": "/v4/", "docs": "/manual/"})
        stale = status_class(root)
        run(root, "build-updated")
        add_case(cases, "contract-invalidation", root, stale_before=stale)

        required_output = root / "public/docs/index.html"
        readonly_delete_adjustment = False
        try:
            required_output.unlink()
        except PermissionError:
            # The generated output is read-only because its source content file
            # is read-only (outputs preserve source permissions). POSIX permits
            # unlinking one from a writable directory; Windows requires its
            # read-only attribute to be cleared first. Retain that distinction
            # below.
            required_output.chmod(stat.S_IWRITE | stat.S_IREAD)
            required_output.unlink()
            readonly_delete_adjustment = True
        stale = status_class(root)
        run(root, "build-updated")
        add_case(cases, "requirement-recovery", root, stale_before=stale,
                 required_output_restored=(root / "public/docs/index.html").is_file())

        write(root / "content/extra.html", "<p>extra</p>\n")
        run(root, "track", "extra", "Extra", "templates/page.html")
        run(root, "build-updated")
        add_case(cases, "tracked-add", root)
        run(root, "mv", "extra", "moved/extra")
        run(root, "build-updated")
        add_case(cases, "tracked-move", root,
                 old_output_absent=not (root / "public/extra.html").exists())
        run(root, "rm", "moved/extra")
        run(root, "build-updated")
        add_case(cases, "tracked-remove", root,
                 removed_output_absent=not (root / "public/moved/extra.html").exists())

        add_case(cases, "template-less-minified-output", root,
                 css=(root / "public/assets/style.css").read_text(encoding="utf-8").strip())
        add_case(cases, "unicode-and-nested-paths", root,
                 unicode_output_present=(root / "public/nested/深い/ページ.html").is_file())

        last_good = normalized_bytes(root / "public/index.html")
        last_metadata = normalized_bytes(root / ".nift/public/index.info.json")
        write_json(root / "data/site.json", {"name": "broken", "items": "not-an-array"})
        failed = run(root, "build-updated", expect=1)
        cases.append({
            "name": "failed-render-preservation",
            "classification": "portable",
            "pass": True,
            "observation": {
                "exit_class": "controlled-failure",
                "diagnostic_class": diagnostic_class(failed),
                "output_preserved": normalized_bytes(root / "public/index.html") == last_good,
                "metadata_preserved": normalized_bytes(root / ".nift/public/index.info.json") == last_metadata,
                "status": status_class(root),
            },
        })
        write_json(root / "data/site.json", {"name": "repaired", "items": [{"value": 4}]})
        run(root, "build-updated")
        add_case(cases, "failed-render-recovery", root)

        good_template = (root / "templates/page.html").read_text(encoding="utf-8")
        last_good = normalized_bytes(root / "public/index.html")
        write(root / "templates/page.html", "@if(true){unterminated\n")
        failed = run(root, "build-updated", expect=1)
        cases.append({
            "name": "malformed-parser-input",
            "classification": "portable",
            "pass": True,
            "observation": {
                "exit_class": "controlled-failure",
                "diagnostic_class": diagnostic_class(failed),
                "output_preserved": normalized_bytes(root / "public/index.html") == last_good,
                "status": status_class(root),
            },
        })
        write(root / "templates/page.html", good_template)
        run(root, "build-updated")
        add_case(cases, "malformed-parser-recovery", root)

    runner = ARGS.runner_os.lower()
    suffix_expected = runner.startswith("windows")
    suffix_present = pathlib.Path(NIFT).suffix.lower() == ".exe"
    cases.append({
        "name": "executable-suffix",
        "classification": "platform-specific",
        "pass": suffix_present == suffix_expected,
        "observation": {"expected_exe_suffix": suffix_expected, "has_exe_suffix": suffix_present},
    })
    cases.append({
        "name": "readonly-output-deletion",
        "classification": "platform-specific",
        "pass": readonly_delete_adjustment == suffix_expected,
        "observation": {
            "expected_write_attribute_adjustment": suffix_expected,
            "write_attribute_adjustment_required": readonly_delete_adjustment,
        },
    })

    passed = all(case["pass"] for case in cases)
    result = {
        "schema_version": 1,
        "checkpoint": "10-cross-platform-behavioural-equivalence",
        "platform": {
            "runner_os": ARGS.runner_os,
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python": platform.python_version(),
            "compiler": compiler_identity(),
            "nift_commit": git_commit(),
        },
        "normalization": ["relative-path-separators-to-slash", "crlf-to-lf-before-output-hash"],
        "cases": cases,
        "portable_case_count": sum(c["classification"] == "portable" for c in cases),
        "platform_specific_case_count": sum(c["classification"] == "platform-specific" for c in cases),
        "pass": passed,
    }
    output = pathlib.Path(ARGS.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if not passed:
        raise SystemExit("checkpoint 10 platform-specific contract failed")
    print(f"checkpoint 10 corpus: PASS ({result['portable_case_count']} portable cases)")
    print(f"evidence={output}")


if __name__ == "__main__":
    main()
