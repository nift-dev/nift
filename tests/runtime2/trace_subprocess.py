"""Capture Host trace separately from the child's stdout and stderr."""
import os
from pathlib import Path
import subprocess


def run_traced(command, trace_path, *, cwd, env, timeout):
    options = dict(cwd=cwd, env=dict(env), stdin=subprocess.DEVNULL, capture_output=True, timeout=timeout)
    if os.name == "nt":
        launcher = Path(__file__).resolve().parents[2] / ".build" / "rt-trace-launcher.exe"
        result = subprocess.run([str(launcher), os.fspath(trace_path), *command], **options)
        if result.returncode == 125 and not Path(trace_path).is_file():
            raise RuntimeError(result.stderr.decode("utf-8", "replace").strip())
        return result
    with open(trace_path, "wb") as trace:
        options["env"]["NIFT_TRACE_HOST"] = str(trace.fileno())
        return subprocess.run(command, pass_fds=(trace.fileno(),), **options)
