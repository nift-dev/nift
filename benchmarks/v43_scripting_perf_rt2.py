#!/usr/bin/env python3
"""Runtime 2.0 variant of benchmarks/v43_scripting_perf.py (L6 `native::`, D-075 #2, D-095, D-096 #2).

benchmarks/v43_scripting_perf.py stays byte-identical. Under the Runtime 2.0 VM default its fs workload
(`touch("f" + i)`) is refused (`native-unqualified`). This variant runs the original module's own main(), with the
same workloads, inputs, sizes, sample counts and timing method, and changes exactly one thing: when the fs workload's
script is written, `touch(` becomes `native::touch(`. Every other script is written byte for byte as the original
writes it.

Finding (2026-11-14): the original times `nift run <script>`. That command line is rejected with "unknown command
'run' and path does not exist" by this candidate and by v4.12.0 alike. So every workload, fs included, measures that
error path; neither the original nor this variant runs its script. --check-equivalence also runs the script
directly (`nift <script>`) to compare the operations themselves. The original keeps its command line (no
re-baselining).

Reporting keeps the two categories apart and never aggregates them:
  identical_source        every workload except fs_10_touch_open: the same source as in v43_scripting_perf.py
  equivalent_operation    fs_10_touch_open: version-adapted source (the call spelling only), the same operations

  NIFT_BIN=... v43_scripting_perf_rt2.py                     the timings, by category
  NIFT_BIN=... v43_scripting_perf_rt2.py --check-equivalence evidence that the adapted fs script has the same
                                                             stdout, stderr, exit status and resulting files as
                                                             the original (the original run with NIFT_VM=0)
"""
import contextlib
import importlib.util
import io
import json
import os
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ADAPTED = "fs.nift"  # the only adapted workload script (fs_10_touch_open)


def adapt(text):
    return text.replace('touch(', 'native::touch(')


def load_original():
    spec = importlib.util.spec_from_file_location("v43_scripting_perf", HERE / "v43_scripting_perf.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def timings():
    original = load_original()
    write = original.write

    def adapted_write(path, text):
        write(path, adapt(text) if pathlib.Path(path).name == ADAPTED else text)
    original.write = adapted_write
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        original.main()
    results = json.loads(buffer.getvalue())
    print(json.dumps({
        "identical_source": {k: v for k, v in results.items() if k != "fs_10_touch_open"},
        "equivalent_operation": {"fs_10_touch_open": results["fs_10_touch_open"]},
        "note": "equivalent_operation differs from v43_scripting_perf.py only by touch( -> native::touch( in "
                "fs.nift; not aggregated with identical_source",
    }, indent=2, sort_keys=True))


def run_fs(nift, text, vm, form):
    d = pathlib.Path(tempfile.mkdtemp(prefix="cp91-eq-"))
    try:
        (d / ADAPTED).write_text(text)
        env = dict(os.environ)
        env.pop("NIFT_VM", None)
        if vm is not None:
            env["NIFT_VM"] = vm
        args = ["run", str(d / ADAPTED)] if form == "benchmark" else [str(d / ADAPTED)]
        p = subprocess.run([nift] + args, cwd=d, env=env, capture_output=True, text=True, timeout=60)
        files = {f.name: f.read_bytes() for f in sorted(d.iterdir()) if f.name != ADAPTED}
        return p.returncode, p.stdout, p.stderr.replace(str(d), "<D>"), files
    finally:
        for f in d.iterdir():
            f.unlink()
        d.rmdir()


def check_equivalence():
    nift = os.environ["NIFT_BIN"]
    captured = {}
    original = load_original()
    original.write = lambda path, text: captured.setdefault(pathlib.Path(path).name, text)
    original.sample = lambda fn, n=15: {}
    made = []
    mkdtemp = tempfile.mkdtemp
    tempfile.mkdtemp = lambda *a, **k: made.append(mkdtemp(*a, **k)) or made[-1]
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            original.main()  # writes nothing (write is captured), times nothing
    finally:
        tempfile.mkdtemp = mkdtemp
        for d in made:
            os.rmdir(d)
    source = captured[ADAPTED]
    ok = True
    # "benchmark": the command line the benchmark times (`nift run <script>`); "direct": `nift <script>`, which
    # runs the script, so the operations themselves are compared.
    for form in ("benchmark", "direct"):
        reference = run_fs(nift, source, "0", form)  # the original source, legacy engine
        print("%s form, original fs.nift (NIFT_VM=0): rc %s stdout %r stderr %r files %s"
              % (form, reference[0], reference[1], reference[2][:120], sorted(reference[3])))
        if form == "direct":
            ok = ok and reference[0] == 0 and sorted(reference[3]) == sorted("f%d" % i for i in range(1, 11))
        for label, vm in (("VM default", None), ("NIFT_VM=0", "0")):
            got = run_fs(nift, adapt(source), vm, form)
            same = got == reference
            ok = ok and same
            print("%s %s form, adapted fs.nift (%s): %s" % ("PASS" if same else "FAIL", form, label,
                  "identical stdout, stderr, exit and files" if same else got))
    others = [n for n, t in captured.items() if n != ADAPTED and adapt(t) != t]
    ok = ok and not others
    print("%s only fs.nift is adapted%s" % ("PASS" if not others else "FAIL", (": also %s" % others) if others else ""))
    print("diff: %r -> %r" % (source, adapt(source)))
    return 0 if ok else 1


if __name__ == "__main__":
    assert os.environ.get("NIFT_BIN"), "NIFT_BIN required"
    if "--check-equivalence" in sys.argv[1:]:
        sys.exit(check_equivalence())
    timings()
