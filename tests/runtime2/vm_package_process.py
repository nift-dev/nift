#!/usr/bin/env python3
"""Package-compatible synchronous process calls under VM defaults and legacy."""
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
    binary = parser.parse_args().nift.resolve()
    executable = Path(sys.executable)
    with tempfile.TemporaryDirectory(prefix="nift-package-process-") as scratch:
        root = Path(scratch)
        allowed = root / "allowed"
        allowed.mkdir()
        program = allowed / "main.f"
        stats = allowed / "vm.jsonl"
        fixture = allowed / "child.py"
        fixture.write_text(
            'import pathlib, sys\n'
            'with pathlib.Path("launches").open("a") as output: output.write("child\\n")\n'
            'print("|".join(sys.argv[1:]))\n', encoding="utf-8")
        launcher = json.dumps(executable.name)
        base = {k: v for k, v in os.environ.items() if not k.startswith("NIFT_")}
        base["PATH"] = str(executable.parent) + os.pathsep + base["PATH"]

        def run(source, imported=True, **settings):
            if imported:
                lines = source.splitlines()
                helpers = "\n".join(line for line in lines if line.startswith("fn("))
                body = "\n".join(line for line in lines if not line.startswith("fn("))
                (allowed / "process_helpers.f").write_text(
                    f"{helpers}\nfn(exercise()) {{\n{body}\n}}\nexport(exercise)\n", encoding="utf-8")
                source = 'import("./process_helpers.f")\nexercise()\n'
            program.write_text(source, encoding="utf-8")
            stats.unlink(missing_ok=True)
            proc = subprocess.run([str(binary), program.name], cwd=allowed, capture_output=True,
                                  text=True, timeout=45, env={**base, "NIFT_VM_STATS": str(stats), **settings})
            rows = [json.loads(raw) for raw in stats.read_text().splitlines()] if stats.exists() else []
            return proc, rows[-1] if rows else {}

        script = (
            'fn(operand_marker(v)) { print(v); return v }\n'
            'items := ["first", "has space"]\n'
            f'r := run({launcher}, "child.py", ...items, operand_marker("last"))\n'
            'print(r.exit_code)\nprint(r.launched)\nprint(r.stdout.trim())\n'
            f's := cmd({launcher}, "child.py", ["written", "in order"]).stdout("result.txt").run()\n'
            'print(s.exit_code)\nprint(s.stdout == "")\n'
        )
        for mode in ({}, {"NIFT_VM": "0"}):
            (allowed / "launches").unlink(missing_ok=True)
            proc, row = run(script, **mode)
            assert (proc.returncode, proc.stdout, proc.stderr) == (
                0, "last\n0\ntrue\nfirst|has space|last\n0\ntrue\n", ""
            ), (mode, proc, row)
            if not mode:
                assert (row.get("vm_executions"), row.get("legacy_executions")) == (1, 0), row
            assert (allowed / "launches").read_text() == "child\nchild\n"
            assert (allowed / "result.txt").read_text() == "written|in order\n"

        (allowed / "launches").unlink()
        proc, _ = run(script, NIFT_NO_PROCESS="1")
        assert proc.returncode == 1 and not proc.stdout and "external process execution disabled" in proc.stderr, proc
        assert not (allowed / "launches").exists()

        redirect = (
            'fn(output_path()) { print("path evaluated"); return "../escaped.txt" }\n'
            f'cmd({launcher}, "child.py").stdout(output_path()).run()\n'
        )
        proc, _ = run(redirect, NIFT_NO_PROCESS="1", NIFT_FS_ROOT=str(allowed))
        assert (proc.returncode, proc.stdout) == (1, "path evaluated\n"), proc
        assert "external process execution disabled" in proc.stderr, proc
        proc, _ = run(redirect, NIFT_FS_ROOT=str(allowed))
        assert proc.returncode == 1 and "path escapes configured filesystem root" in proc.stderr, proc
        assert not (root / "escaped.txt").exists() and not (allowed / "launches").exists()

        (allowed / "over_limit.py").write_text(
            'import pathlib, time\n'
            'print("output past the limit", flush=True)\n'
            'time.sleep(3)\n'
            'pathlib.Path("late-marker").touch()\n', encoding="utf-8")
        proc, _ = run(f'run({launcher}, "over_limit.py")\n', NIFT_READ_LIMIT="4")
        assert proc.returncode == 1 and "exceeds the read limit of 4 bytes" in proc.stderr, proc
        assert not (allowed / "late-marker").exists()

        for source, reason in (
            ('print("before")\nrun("echo hi")\n', "callable:run"),
            (f'print("before")\nc := cmd({launcher}, "child.py")\nc.run()\n', "callable:cmd"),
        ):
            for imported in (True, False):
                proc, row = run(source, imported=imported)
                assert proc.returncode == 1 and not proc.stdout and f"({reason})" in proc.stderr, proc
                assert (row.get("vm_executions"), row.get("legacy_executions")) == (0, 0), row
        proc, row = run(f'run({launcher}, "child.py")\n', imported=False)
        assert proc.returncode == 1 and "(callable:run)" in proc.stderr, proc
        assert (row.get("vm_executions"), row.get("legacy_executions")) == (0, 0), row

        base["PATH"] = str(allowed) + os.pathsep + base["PATH"]
        if os.name == "nt":
            (allowed / "text-executable.cmd").write_text(
                '@echo accidental > shell-fallback\n', encoding="utf-8")
            proc, _ = run('run("text-executable.cmd")\n')
            assert proc.returncode == 1 and "expected a native Windows executable" in proc.stderr, proc
        else:
            bare = allowed / "text-executable"
            bare.write_text('echo accidental > shell-fallback\n', encoding="utf-8")
            bare.chmod(0o700)
            proc, _ = run('r := run("text-executable")\nprint(r.exit_code)\nprint(r.launched)\n')
            assert (proc.returncode, proc.stdout, proc.stderr) == (0, "126\nfalse\n", ""), proc
        assert not (allowed / "shell-fallback").exists()
        assert not (allowed / "launches").exists()
    print("PASS package process execution and refusal controls")


if __name__ == "__main__":
    main()
