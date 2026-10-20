#!/usr/bin/env python3
"""CP16 engine parity of dependency recording (owner requirement, 2026-10-20).

Every recorded read is performed once per execution context and once per
engine, and the recorded dependency sets are compared:

  reads     open, open_bytes, cat, sha256_file, file().open("r"/"rw"),
            ifstream, a failed open (absence), exists/is_file/is_dir/stat,
            ls(dir), ls(glob), getenv, env(), platform()
  contexts  page @script, a function, an imported module, an async task,
            a thread, a pre-build hook, a custom build script, a post-build
            hook, and a standalone script (which records nothing: there the
            Host-op stream, trace v2, is compared instead)
  engines   legacy (NIFT_VM unset) and VM (NIFT_VM=1)

Recording has one implementation for every engine (src/PageObserver.*): the
engine only decides whether a context observes. The comparison is per case:
page-info dependency fields (dependencies, reqs, absent, kinds, listings,
environment, environment-snapshot, platform, dependency-hashes) after a clean
`nift build --all` in hash mode, or the v2 Host-op stream for scripts.

Which engine actually ran is reported per case, never assumed: the VM runs
standalone scripts only and refuses units it cannot compile (NIFT_VM=1 then
falls back to legacy; NIFT_VM_STATS records the outcome). A case where the VM
did not execute is reported as `vm: not engaged (<reason>)`: its comparison
holds but proves nothing about the VM yet. `--require-vm` turns that into a
failure (for when the VM runs host natives and page/build contexts).

`--expected DIR` additionally compares every legacy recording with a stored
capture (`--write` stores one), so a change of recording in any context is
seen even while the VM is not engaged.

Usage: engine_parity.py --nift BIN [--require-vm] [--expected DIR [--write]] [--only read,context]
D-031: every nift run under RLIMIT_FSIZE/RLIMIT_AS, a timeout, own session.
"""
import argparse
import json
import os
import resource
import shutil
import signal
import subprocess
import sys
import tempfile

READS = {
    "open": 'xa := open("data/r.txt")',
    "open_bytes": 'xb := open_bytes("data/r.txt")',
    "cat": 'cat("data/r.txt")',
    "sha256_file": 'xs := sha256_file("data/r.txt")',
    "file_r": 'ff := file("data/r.txt"); ff.open("r"); xf := ff.read_all(); ff.close()',
    "file_rw": 'fw := file("data/rw.txt"); fw.open("rw"); xw := fw.read_all(); fw.close()',
    "ifstream": 'si := ifstream("data/r.txt"); xi := si.read_line(); close(si)',
    "open_missing": 'xm := ""; try { xm = open("data/missing.txt") } catch(e) { xm = "none" }',
    "exists_missing": 'xe := exists("data/missing.txt")',
    "is_file": 'xq := is_file("data/r.txt")',
    "is_dir": 'xd := is_dir("data")',
    "stat": 'xt := stat("data/r.txt")',
    "ls_dir": 'xl := ls("data")',
    "ls_glob": 'xg := ls("data/*.txt")',
    "getenv": 'xv := getenv("CP16_PARITY_VAR")',
    "env": 'xn := env()',
    "platform": 'xp := platform()',
}
CONTEXTS = ["page", "function", "module", "async", "thread", "pre_build", "build", "post_build", "script"]
DEP_FIELDS = ["dependencies", "reqs", "absent", "kinds", "listings", "environment",
              "environment-snapshot", "platform", "dependency-hashes"]
TIMEOUT = 60


def bounded():
    for kind, value in ((resource.RLIMIT_FSIZE, 256 << 20), (resource.RLIMIT_AS, 4 << 30)):
        hard = resource.getrlimit(kind)[1]
        if hard != resource.RLIM_INFINITY:
            value = min(value, hard)
        resource.setrlimit(kind, (value, value))


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


def output_script(text):
    return ('out_file := file(getenv("NIFT_HOOK_OUTPUT"))\nout_file.open("w")\nout_file.write(%s)\n'
            'out_file.save()\nout_file.close()\n' % json.dumps(text))


def make_case(root, read, context):
    """Writes the project (or script) for one case; returns the nift arguments."""
    stmt = READS[read]
    write(os.path.join(root, "data", "r.txt"), "r-1\n")
    write(os.path.join(root, "data", "rw.txt"), "rw-1\n")
    if context == "script":
        write(os.path.join(root, "main.f"), stmt + "\nprint(\"ok\")\n")
        return ["main.f"]
    entry = {"name": "a", "title": "a", "template": ""}
    page = 'R:@script { %s; return "ok" }\n' % stmt
    if context == "function":
        page = '@fn(rdF()) { %s; return "ok" }\nR:@script { return rdF() }\n' % stmt
    elif context == "module":
        write(os.path.join(root, "lib", "m.f"), 'fn(rdM()) { %s; return "ok" }\nexport(rdM)\n' % stmt)
        page = 'R:@script { import("../lib/m.f"); return rdM() }\n'
    elif context == "async":
        page = 'R:@script { gN := async () => { %s; return "ok" }; return await gN() }\n' % stmt
    elif context == "thread":
        page = '@fn(rdT()) { %s; return "ok" }\nR:@script { tT := thread(rdT); return tT.join() }\n' % stmt
    elif context in ("pre_build", "post_build"):
        key = "pre-build" if context == "pre_build" else "post-build"
        write(os.path.join(root, "hook.f"), stmt + "\n")
        entry[key] = "hook.f"
        page = "R:plain\n"
    elif context == "build":
        write(os.path.join(root, "main.f"), stmt + "\n" + output_script("R:ok\n"))
        entry["build"] = "main.f"
        page = "R:unused\n"
    write(os.path.join(root, "content", "a.html"), page)
    os.makedirs(os.path.join(root, "public"), exist_ok=True)
    write(os.path.join(root, ".nift", "config.json"), json.dumps({"config": {
        "content-dir": "content/", "content-ext": ".html", "output-dir": "public/", "output-ext": ".html",
        "default-template": "", "build-threads": 1, "incremental-mode": "hash", "minify-exts": []}}))
    write(os.path.join(root, ".nift", "tracked.json"), json.dumps({"tracked": [entry]}))
    return ["build", "--all"]


def run_nift(nift, root, args, engine, stats_path):
    env = {"PATH": "/usr/local/bin:/usr/bin:/bin", "HOME": root, "LC_ALL": "C", "TZ": "UTC",
           "CP16_PARITY_VAR": "v-1", "NIFT_VM_STATS": stats_path}
    if engine == "vm":
        env["NIFT_VM"] = "1"
    rfd, wfd = os.pipe()
    os.set_inheritable(wfd, True)
    env["NIFT_TRACE_HOST"] = str(wfd)
    env["NIFT_TRACE_HOST_VERSION"] = "2"
    proc = subprocess.Popen([nift] + args, cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            pass_fds=(wfd,), start_new_session=True, preexec_fn=bounded)
    os.close(wfd)
    chunks = []
    try:
        out, err = proc.communicate(timeout=TIMEOUT)
        code = proc.returncode
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        out, err = proc.communicate()
        code = "timeout"
    try:
        os.killpg(proc.pid, signal.SIGKILL)  # nothing of this case outlives it (by process group, never by name)
    except ProcessLookupError:
        pass
    while True:
        data = os.read(rfd, 65536)
        if not data:
            break
        chunks.append(data)
    os.close(rfd)
    return code, out.decode(errors="replace"), err.decode(errors="replace"), b"".join(chunks).decode(errors="replace")


def norm(text, root):
    real = os.path.realpath(root)
    for p in sorted({real, root}, key=len, reverse=True):
        text = text.replace(p, "$ROOT")
    return text


def recording(root, context):
    path = os.path.join(root, ".nift", "public", "a.info.json")
    if context == "script" or not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as handle:
        info = json.load(handle)
    out = {}
    for key in DEP_FIELDS:
        if key in info:
            value = info[key]
            if key == "environment-snapshot":
                value = "$FP"  # hashes the whole environment, which contains $ROOT
            elif key == "listings":
                # glob: fingerprints hash absolute paths (per-run roots); dir: ones are names only
                value = {k: ("$FP" if k.startswith("glob:") else v) for k, v in value.items()}
            out[key] = json.loads(norm(json.dumps(value), root))
    return out


def host_ops(trace, root):
    # The v2 Host-op stream without context labels (async/thread labels are
    # numbered per engine run) -- what was read, in order.
    ops = []
    for line in trace.splitlines():
        try:
            record = json.loads(line)
        except ValueError:
            ops.append("unparsable: " + line)
            continue
        record.pop("ctx", None)
        ops.append(norm(json.dumps(record, sort_keys=True), root))
    return ops


def vm_engaged(stats_path):
    if not os.path.exists(stats_path):
        return False, "never asked (no standalone script unit)"
    outcomes = []
    with open(stats_path, encoding="utf-8") as handle:
        for line in handle:
            try:
                record = json.loads(line)
            except ValueError:
                continue
            outcomes.append(record)
    executed = [r for r in outcomes if r.get("vm_executions", 0) > 0 or r.get("outcome") == "executed"]
    if executed:
        return True, "executed"
    reasons = sorted({"%s:%s" % (r.get("outcome"), r.get("reason")) for r in outcomes})
    return False, ", ".join(reasons) or "no stats"


def run_case(nift, parent, read, context):
    result = {}
    for engine in ("legacy", "vm"):
        root = tempfile.mkdtemp(prefix="cp16-parity-", dir=parent)
        try:
            args = make_case(root, read, context)
            stats = os.path.join(parent, "stats-%s-%s-%s.jsonl" % (read, context, engine))
            if os.path.exists(stats):
                os.remove(stats)
            code, out, err, trace = run_nift(nift, root, args, engine, stats)
            entry = {"exit": code, "stderr": norm(err, root).strip()}
            entry["recording"] = recording(root, context)
            entry["ops"] = host_ops(trace, root)
            entry["output"] = None
            page_out = os.path.join(root, "public", "a.html")
            if os.path.exists(page_out):
                with open(page_out, encoding="utf-8", errors="replace") as handle:
                    entry["output"] = handle.read()
            elif context == "script":
                entry["output"] = norm(out, root)
            if engine == "vm":
                entry["vm"] = vm_engaged(stats)
                if os.path.exists(stats):
                    os.remove(stats)
            result[engine] = entry
        finally:
            shutil.rmtree(root, ignore_errors=True)
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--require-vm", action="store_true")
    ap.add_argument("--expected", default="")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--only", default="", help="comma-separated read or context names")
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    only = set(filter(None, a.only.split(",")))
    parent = tempfile.mkdtemp(prefix="cp16-parity-")
    failures = []
    not_engaged = 0
    captured = {}
    try:
        for read in READS:
            for context in CONTEXTS:
                if only and read not in only and context not in only:
                    continue
                case = "%s/%s" % (read, context)
                r = run_case(nift, parent, read, context)
                legacy, vm = r["legacy"], r["vm"]
                problems = []
                if legacy["exit"] != 0:
                    problems.append("legacy run failed (%s): %s" % (legacy["exit"], legacy["stderr"][:300]))
                for field in ("exit", "recording", "output"):
                    if legacy[field] != vm[field]:
                        problems.append("%s differs: legacy %r, vm %r" % (field, legacy[field], vm[field]))
                if context == "script" and legacy["ops"] != vm["ops"]:
                    problems.append("host-op stream differs:\n  legacy %s\n  vm     %s" % (legacy["ops"], vm["ops"]))
                engaged, why = vm["vm"]
                if not engaged:
                    not_engaged += 1
                    if a.require_vm:
                        problems.append("vm not engaged (%s)" % why)
                captured[case] = {"recording": legacy["recording"], "output": legacy["output"],
                                  "ops": legacy["ops"] if context == "script" else None}
                if a.expected and not a.write:
                    path = os.path.join(a.expected, read, context + ".json")
                    if not os.path.exists(path):
                        problems.append("no expected capture %s" % path)
                    else:
                        with open(path, encoding="utf-8") as handle:
                            want = json.load(handle)
                        if want != captured[case]:
                            problems.append("legacy recording differs from %s:\n  expected %s\n  got      %s" %
                                            (path, json.dumps(want, sort_keys=True), json.dumps(captured[case], sort_keys=True)))
                status = "DIFF" if problems else "OK"
                print("%s %s  (vm: %s)" % (status, case, "engaged" if engaged else "not engaged: " + why))
                for p in problems:
                    print("    " + p)
                if problems:
                    failures.append(case)
        if a.expected and a.write:
            for case, value in captured.items():
                read, context = case.split("/")
                path = os.path.join(a.expected, read, context + ".json")
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w", encoding="utf-8") as handle:
                    json.dump(value, handle, indent=1, sort_keys=True)
                    handle.write("\n")
    finally:
        shutil.rmtree(parent, ignore_errors=True)
    total = len(captured)
    print("engine parity: %d cases, %d differences, vm not engaged in %d" % (total, len(failures), not_engaged))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
