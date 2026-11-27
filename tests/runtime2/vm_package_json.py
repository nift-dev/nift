#!/usr/bin/env python3
"""Bounded process-produced JSON injection through the default VM."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nift", type=Path, required=True)
    parser.add_argument("--mutant", action="store_true")
    opts = parser.parse_args()
    binary = opts.nift.resolve()
    if sys.platform == "win32":
        print("SKIP package process JSON shell fixture on Windows")
        return
    if opts.mutant:
        from sanctioned_mutant import build
        with tempfile.TemporaryDirectory(prefix="nift-package-json-mutant-") as scratch:
            mutant = build(Path(scratch) / "object", edits=[
                ("src/rt/compiler_process.cpp", "            return succeeded;",
                 "            return succeeded || process_result != nullptr;"),
            ])
            proc = subprocess.run([sys.executable, "-B", __file__, "--nift", str(mutant)],
                                  capture_output=True, text=True, timeout=90)
            if proc.returncode != 1 or "guardless injection ran" not in proc.stderr:
                raise SystemExit(f"package JSON admission mutant lacked its expected verdict: {proc.stderr}")
            print("PASS package JSON admission mutant")
        return
    with tempfile.TemporaryDirectory(prefix="nift-package-json-") as scratch:
        root = Path(scratch)
        fixture = root / "redis-cli"
        fixture.write_text(
            '#!/bin/sh\n'
            'printf "launch\\n" >> launches\n'
            'case "$2" in\n'
            '  good) printf \'[{"count":7,"text":"hello","ok":true}]\\n\' ;;\n'
            '  string) printf \'"hello"\' ;;\n'
            "  escape) printf '%s' '\"\\u00e9\"' ;;\n"
            "  del) printf '\"\\177\"' ;;\n"
            '  bad) printf \'{"unterminated":\' ;;\n'
            '  expression) printf \'"$[print(99)]"\' ;;\n'
            '  missing) rm -f -- "$3" ;;\n'
            "  large) awk 'BEGIN { printf \"[\"; for(i=0;i<5000;i++) "
            "printf \"%s{\\\"x\\\":7}\", (i?\",\":\"\"); print \"]\" }' ;;\n"
            'esac\n', encoding="utf-8")
        fixture.chmod(0o755)
        (root / "sqlite3").symlink_to(fixture.name)
        (root / "main.f").write_text('import("m.f")\nb := Box()\nb.read()\n', encoding="utf-8")
        base = {key: value for key, value in os.environ.items() if not key.startswith("NIFT_")}
        base["PATH"] = str(root) + os.pathsep + base["PATH"]

        def run(kind, inject="inject(temp)", guard=True, path="stdout.json", intervening="", backend="redis-cli", **env):
            (root / "launches").unlink(missing_ok=True)
            (root / "stats").unlink(missing_ok=True)
            option = "--json" if backend == "redis-cli" else "-json"
            body = 'if(result.exit_code != 0) { return null }\n' if guard else ""
            if backend == "sqlite3":
                body += 'if(open(temp).trim().empty()) { return null }\n'
            expected = {"string": '"hello"', "escape": '"u00e9"'}.get(
                kind, '[{"count":7,"text":"hello","ok":true}]')
            render = 'print(source)' if kind == "del" else f'print(source == {expected})'
            (root / "m.f").write_text(
                'struct(Box) {\nfn(open(path)) { return "" }\nfn(read()) {\n'
                f'temp := {json.dumps(path)}\n'
                f'result := cmd("{backend}", "{option}", "{kind}", temp).stdout(temp).run()\n'
                f'{body}{intervening}source := {inject}\n'
                f'{render}\nreturn null\n'
                '}\n}\nexport(Box)\n', encoding="utf-8")
            with (root / "trace").open("w") as trace:
                fd = trace.fileno()
                proc = subprocess.run(
                    [str(binary), "main.f"], cwd=root, capture_output=True, text=True, timeout=30,
                    env={**base, "NIFT_VM_STATS": "stats", "NIFT_TRACE_HOST": str(fd),
                         "NIFT_TRACE_HOST_VERSION": "2", **env}, pass_fds=(fd,))
            stats = root / "stats"
            counters = json.loads(stats.read_text().splitlines()[-1]) if stats.exists() else {}
            counters["inject_reads"] = sum(json.loads(line).get("op") == "inject"
                                            for line in (root / "trace").read_text().splitlines())
            return proc, counters

        for backend, mode in (("redis-cli", {}), ("redis-cli", {"NIFT_VM": "0"}),
                              ("sqlite3", {}), ("sqlite3", {"NIFT_VM": "0"})):
            result, stats = run("good", backend=backend, **mode)
            assert result.returncode == 0 and not result.stderr, (result, stats)
            assert result.stdout == "true\n", (result, stats)
            assert (root / "launches").read_text() == "launch\n"
            if not mode:
                assert (stats["vm_executions"], stats["legacy_executions"], stats["inject_reads"]) == (1, 0, 1), stats
        for scalar, output in (("string", "true\n"), ("escape", "true\n"), ("del", "\x7f\n")):
            for mode in ({}, {"NIFT_VM": "0"}):
                result, stats = run(scalar, **mode)
                assert (result.returncode, result.stdout, result.stderr) == (0, output, ""), (result, stats)
        for kind, diagnostic in (("bad", "inject:"), ("expression", "inject: JSON string interpolation is unsupported"),
                                 ("missing", "inject: source is not readable")):
            result, stats = run(kind)
            assert result.returncode != 0 and not result.stdout and diagnostic in result.stderr, (result, stats)
            assert stats["legacy_executions"] == 0, stats
            assert (root / "launches").read_text() == "launch\n"
        result, stats = run("large", NIFT_VM_HEAP_BUDGET="150000")
        assert result.returncode != 0 and not result.stdout and "memory" in result.stderr.lower(), (result, stats)
        assert stats["legacy_executions"] == 0 and (root / "launches").read_text() == "launch\n"
        for settings in ({"guard": False}, {"inject": 'inject("stdout.json")'},
                         {"intervening": 'print("unexpected")\n'}):
            result, stats = run("good", **settings)
            assert result.returncode != 0 and stats["reason"] == "callable:inject", (
                "guardless injection ran" if settings == {"guard": False} and result.returncode == 0 else settings,
                result, stats)
            assert not result.stdout and not (root / "launches").exists()
        (root / "~").mkdir()
        (root / "nested").mkdir()
        for path in ("~/redirect.json", "nested/../redirect.json"):
            result, stats = run("good", path=path)
            assert result.returncode != 0 and "unambiguous process output path" in result.stderr, (result, stats)
            assert not result.stdout
        result, stats = run("good", path="../escaped.json", NIFT_FS_ROOT=str(root))
        assert result.returncode != 0 and "path escapes configured filesystem root" in result.stderr, (result, stats)
        assert not (root / "launches").exists()
    print("PASS package process JSON")


if __name__ == "__main__":
    main()
