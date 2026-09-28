#!/usr/bin/env python3
"""Reproducible CP14 evidence for Nift's current arbitrary-byte surfaces."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


NIFT = str(Path(sys.argv[1] if len(sys.argv) > 1 else "./nift").resolve())
ROOT = Path(__file__).resolve().parents[1]
TIME = shutil.which("time") or "/usr/bin/time"
results = {}


def run(work, name, source, expected=0):
    path = work / (name + ".f")
    path.write_text(source, encoding="utf-8")
    result = subprocess.run([NIFT, str(path)], cwd=work, capture_output=True)
    if result.returncode != expected:
        raise AssertionError((name, result.returncode, result.stdout, result.stderr))
    return result


def measured(work, name, source):
    path = work / (name + ".f")
    path.write_text(source, encoding="utf-8")
    metrics = work / (name + ".metrics")
    result = subprocess.run(
        [TIME, "-f", "%e %M", "-o", str(metrics), NIFT, str(path)],
        cwd=work, capture_output=True,
    )
    if result.returncode != 0:
        raise AssertionError((name, result.returncode, result.stdout, result.stderr))
    elapsed, rss = metrics.read_text(encoding="ascii").split()
    return {"elapsed_seconds": float(elapsed), "peak_rss_kib": int(rss)}


with tempfile.TemporaryDirectory(prefix="nift-cp14-") as temporary:
    work = Path(temporary)
    one_mib = bytes(range(256)) * 4096
    sixteen_mib = one_mib * 16
    (work / "all.bin").write_bytes(one_mib)
    (work / "large.bin").write_bytes(sixteen_mib)
    (work / "nul.bin").write_bytes(b"A\x00B")
    (work / "invalid.bin").write_bytes(b"A\xffB")
    (work / "marker.bin").write_bytes(b"\x1fnift:file:not-a-handle")

    stream_reads = "\n".join("chunked.write(input.read(65536))" for _ in range(16))
    run(work, "roundtrip", r'''
raw := open("all.bin")
direct := ofstream("open.out")
direct.write(raw)
close(direct)
input := ifstream("all.bin")
chunked := ofstream("stream.out")
''' + stream_reads + r'''
close(input)
close(chunked)
managed := file("all.bin")
managed.open("r")
managed_out := ofstream("managed.out")
managed_out.write(managed.read_all())
close(managed_out)
managed.close()
copy("all.bin", "copy.out")
fn(identity(value)) { return value }
t := thread(identity, raw)
threaded := ofstream("thread.out")
threaded.write(t.join())
close(threaded)
''')
    for output in ("open.out", "stream.out", "managed.out", "copy.out", "thread.out"):
        observed = (work / output).read_bytes()
        if observed != one_mib:
            mismatch = next(
                (index for index, pair in enumerate(zip(observed, one_mib)) if pair[0] != pair[1]),
                min(len(observed), len(one_mib)),
            )
            raise AssertionError(
                f"{output} changed arbitrary bytes: {len(observed)} vs {len(one_mib)}, first {mismatch}",
            )
    results["all_octet_roundtrips"] = {
        "bytes": len(one_mib),
        "surfaces": ["open/ofstream", "ifstream chunks", "managed file", "copy", "thread"],
        "byte_exact": True,
    }

    invalid_length = run(
        work, "invalid-length", 'value := open("invalid.bin")\nprint(value.length())\n', expected=1,
    )
    invalid_length_diagnostic = invalid_length.stderr.decode("utf-8", "replace").strip()
    if "invalid UTF-8" not in invalid_length_diagnostic:
        raise AssertionError(f"unexpected invalid UTF-8 diagnostic: {invalid_length_diagnostic}")
    results["invalid_utf8_length"] = {
        "failed": True,
        "diagnostic": invalid_length_diagnostic,
    }
    run(work, "invalid-substr", r'''
value := open("invalid.bin")
output := ofstream("invalid-substr.out")
output.write(value.substr(0, 3))
close(output)
''')
    invalid_substr_exact = (work / "invalid-substr.out").read_bytes() == b"A\xffB"
    if not invalid_substr_exact:
        raise AssertionError("substr changed invalid UTF-8 bytes")
    results["invalid_utf8_substr_byte_exact"] = invalid_substr_exact

    run(work, "nul-json", r'''
output := ofstream("nul.json")
output.write_val(open("nul.bin"))
close(output)
''')
    nul_json = (work / "nul.json").read_text(encoding="utf-8")
    nul_json_roundtrip = json.loads(nul_json) == "A\x00B"
    if not nul_json_roundtrip:
        raise AssertionError("embedded NUL did not round-trip through JSON")
    results["nul_json_roundtrip"] = nul_json_roundtrip
    run(work, "invalid-json", r'''
output := ofstream("invalid.json")
output.write_val(open("invalid.bin"))
close(output)
''')
    invalid_json = (work / "invalid.json").read_bytes()
    try:
        json.loads(invalid_json)
        invalid_json_accepted = True
    except (UnicodeDecodeError, json.JSONDecodeError):
        invalid_json_accepted = False
    if b"\xff" not in invalid_json or invalid_json_accepted:
        raise AssertionError("invalid UTF-8 JSON behavior changed")
    results["invalid_utf8_json"] = {
        "contains_raw_ff": b"\xff" in invalid_json,
        "accepted_by_python_json": invalid_json_accepted,
    }

    marker = run(work, "marker", r'''
value := open("marker.bin")
output := ofstream("marker.out")
output.write(value)
    close(output)
''', expected=1)
    marker_diagnostic = marker.stderr.decode("utf-8", "replace").strip()
    if "not directly renderable" not in marker_diagnostic:
        raise AssertionError(f"unexpected opaque marker diagnostic: {marker_diagnostic}")
    results["opaque_marker_collision"] = {
        "failed": True,
        "diagnostic": marker_diagnostic,
    }

    (work / "emit.py").write_text(
        "import pathlib,sys\nsys.stdout.buffer.write(pathlib.Path('all.bin').read_bytes())\n",
        encoding="utf-8",
    )
    run(work, "process", r'''
result := run("python3", "emit.py")
output := ofstream("process.out")
output.write(result.stdout)
close(output)
''')
    process_capture_exact = (work / "process.out").read_bytes() == one_mib
    if not process_capture_exact:
        raise AssertionError("process capture changed arbitrary bytes")
    results["process_capture_byte_exact"] = process_capture_exact

    library = work / "libfixture.so"
    subprocess.run(
        ["cc", "-std=c99", "-fPIC", "-shared", str(ROOT / "tests/ffi/fixture.c"), "-o", str(library)],
        check=True, capture_output=True,
    )
    ffi = run(work, "ffi-alias", r'''
lib := ffi_open("./libfixture.so")
original := ffi_buffer([1, 2, 3])
alias := deepcopy(original)
ffi_call(lib, "nift_ffi_buffer_xor", "void(buffer,u64,u8)", alias, 3, 255)
values := ffi_bytes(original)
print(values[0]); print(values[1]); print(values[2])
''')
    ffi_deepcopy_aliases = ffi.stdout.decode().strip() == "254\n253\n252"
    if not ffi_deepcopy_aliases:
        raise AssertionError("FFI deepcopy no longer aliases mutable buffer storage")
    results["ffi_deepcopy_aliases"] = ffi_deepcopy_aliases
    ffi_thread = run(work, "ffi-thread", r'''
fn(identity(value)) { return value }
buffer := ffi_buffer([1, 2, 3])
t := thread(identity, buffer)
print(t.join())
''', expected=1)
    ffi_thread_diagnostic = ffi_thread.stderr.decode("utf-8", "replace").strip()
    if "non-transferable resource" not in ffi_thread_diagnostic:
        raise AssertionError(f"unexpected FFI thread diagnostic: {ffi_thread_diagnostic}")
    results["ffi_buffer_thread_transfer"] = {
        "rejected": True,
        "diagnostic": ffi_thread_diagnostic,
    }

    results["performance"] = {
        "baseline": measured(work, "baseline", "value := 1\n"),
        "open_write_16_mib": measured(work, "open-write-large", r'''
value := open("large.bin")
output := ofstream("large-open.out")
output.write(value)
close(output)
'''),
        "managed_read_write_16_mib": measured(work, "managed-large", r'''
value := file("large.bin")
value.open("r")
output := ofstream("large-managed.out")
output.write(value.read_all())
close(output)
value.close()
'''),
        "ffi_to_array_1_mib": measured(work, "ffi-large", r'''
buffer := ffi_buffer(open("all.bin"))
values := ffi_bytes(buffer)
print(values.size())
'''),
    }

print(json.dumps(results, indent=2, sort_keys=True))
