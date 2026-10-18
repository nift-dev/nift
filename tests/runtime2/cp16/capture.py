#!/usr/bin/env python3
"""CP16 legacy probe capture (design fixtures; not wired into the Makefile).

Runs every probe under probes/ with a `nift` binary and records, per probe,
the observable channels the CP16 Host-op layer must reproduce:

  scripts: stdout, stderr, exit, trace.jsonl (NIFT_TRACE_HOST), files.txt
  sites:   steps.txt (exit/stdout/stderr per step), trace.jsonl (per step),
           deps.json (dependency sections of page-info per `info` step),
           files.txt

Usage:
  capture.py --nift BIN [--only id,id] [--write]   # capture; --write stores expected/
  capture.py --nift BIN --check [--only ...]       # compare with expected/ (exit 1 on any difference)
  capture.py --nift BIN --check --mutant MODE ...  # self-check: must report DIFF (see mutant.py)

Every probe is run --runs times (default 2), each in a fresh base directory
with a different absolute path; the normalised results must be identical,
which proves the probe deterministic and the normalisations sufficient.

Layout of a run's base directory (normalised to $BASE everywhere):
  $BASE/w     the probe's working directory (script dir / site root)
  $BASE/ext   an empty sibling for "outside the project" cases
  $BASE/home  HOME;  $BASE/tmp  TMPDIR

Probe directory files (not copied into w): setup.sh (run with sh in w,
BASE exported), env (KEY=VALUE lines added to the environment), stdin,
args (one argument per line), priv (`nodac`: run without
CAP_DAC_OVERRIDE/CAP_DAC_READ_SEARCH via setpriv so permission errors are
real even as root; Linux only, otherwise the probe is SKIPPED), normalize
(REGEX<TAB>REPLACEMENT lines applied to stdout/stderr and output files),
steps (sites only). Everything else is copied into w.

Normalisations (complete list): the base path (and its realpath) -> $BASE;
a whole line `time taken: <number> seconds` -> `time taken: $T seconds`;
page-info listing fingerprints and the environment-snapshot fingerprint ->
$FP (they hash absolute paths / the environment, which holds $BASE paths);
.nift/ files are listed by name only (their dependency content is deps.json);
the probe's own `normalize` rules. Nothing else is masked.
"""
import argparse
import difflib
import hashlib
import json
import os
import re
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
PROBES = os.path.join(HERE, "probes")
EXPECTED = os.path.join(HERE, "expected")
META = {"setup.sh", "env", "stdin", "args", "priv", "normalize", "steps", "NOTES"}
TIMEOUT = 30
MUTANT = ""  # --mutant MODE: run through mutant.py (self-check of the comparison)
TIME_TAKEN = re.compile(r"^time taken: [0-9.e+-]+ seconds$", re.M)
DEP_FIELDS = ["dependencies", "reqs", "absent", "kinds", "listings", "environment",
              "environment-snapshot", "platform", "dependency-hashes"]


def read_lines(path):
    if not os.path.exists(path):
        return []
    with open(path, encoding="utf-8") as handle:
        return [line.rstrip("\n") for line in handle if line.strip() and not line.startswith("#")]


class Probe:
    def __init__(self, kind, pid, path):
        self.kind, self.id, self.path = kind, pid, path
        self.env = dict(l.split("=", 1) for l in read_lines(os.path.join(path, "env")))
        self.args = read_lines(os.path.join(path, "args"))
        self.priv = (read_lines(os.path.join(path, "priv")) or [""])[0]
        self.rules = []
        for line in read_lines(os.path.join(path, "normalize")):
            regex, repl = line.split("\t", 1)
            self.rules.append((re.compile(regex, re.M), repl))
        stdin = os.path.join(path, "stdin")
        self.stdin = open(stdin, "rb").read() if os.path.exists(stdin) else b""
        self.steps = read_lines(os.path.join(path, "steps")) if kind == "sites" else []


def discover(only):
    probes = []
    for kind in ("scripts", "sites"):
        top = os.path.join(PROBES, kind)
        for pid in sorted(os.listdir(top)):
            if only and pid not in only:
                continue
            probes.append(Probe(kind, pid, os.path.join(top, pid)))
    if only:
        missing = set(only) - {p.id for p in probes}
        if missing:
            sys.exit("unknown probe(s): " + ", ".join(sorted(missing)))
    return probes


def wrapper(probe):
    if probe.priv != "nodac":
        return []
    if not sys.platform.startswith("linux") or not shutil.which("setpriv"):
        return None
    return ["setpriv", "--bounding-set", "-dac_override,-dac_read_search"]


class Run:
    def __init__(self, probe, nift, parent):
        self.probe, self.nift = probe, os.path.abspath(nift)
        self.base = tempfile.mkdtemp(prefix="cp16-", dir=parent)
        self.real = os.path.realpath(self.base)
        for d in ("w", "ext", "home", "tmp"):
            os.mkdir(os.path.join(self.base, d))
        self.w = os.path.join(self.base, "w")
        src = os.path.join(probe.path, "site") if probe.kind == "sites" else probe.path
        for name in os.listdir(src):
            if name in META and probe.kind == "scripts":
                continue
            s, d = os.path.join(src, name), os.path.join(self.w, name)
            (shutil.copytree(s, d, symlinks=True) if os.path.isdir(s) else shutil.copy2(s, d, follow_symlinks=False))
        self.env = {"PATH": "/usr/local/bin:/usr/bin:/bin", "HOME": os.path.join(self.base, "home"),
                    "TMPDIR": os.path.join(self.base, "tmp"), "LC_ALL": "C", "LANG": "C", "TZ": "UTC",
                    "BASE": self.base}
        self.env.update({k: v.replace("$BASE", self.base) for k, v in probe.env.items()})
        setup = os.path.join(probe.path, "setup.sh")
        if os.path.exists(setup):
            subprocess.run(["sh", setup], cwd=self.w, env=self.env, check=True)

    def norm(self, text, probe_rules=True):
        if isinstance(text, bytes):
            text = text.decode("utf-8", "backslashreplace")
        for p in sorted({self.real, self.base}, key=len, reverse=True):
            text = text.replace(p, "$BASE")
        text = TIME_TAKEN.sub("time taken: $T seconds", text)
        if probe_rules:
            for regex, repl in self.probe.rules:
                text = regex.sub(repl, text)
        return text

    def nift_run(self, args, stdin=b""):
        rfd, wfd = os.pipe()
        os.set_inheritable(wfd, True)
        env = dict(self.env)
        env["NIFT_TRACE_HOST"] = str(wfd)
        mutant = [sys.executable, os.path.join(HERE, "mutant.py"), MUTANT] if MUTANT else []
        cmd = (wrapper(self.probe) or []) + mutant + [self.nift] + args
        proc = subprocess.Popen(cmd, cwd=self.w, env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, pass_fds=(wfd,), start_new_session=True)
        os.close(wfd)
        trace = []
        reader = threading.Thread(target=lambda: trace.append(_drain(rfd)))
        reader.start()
        try:
            out, err = proc.communicate(stdin, timeout=TIMEOUT)
            code = proc.returncode
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            out, err = proc.communicate()
            code = "timeout"
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        reader.join()
        os.close(rfd)
        return code, out, err, trace[0]

    def snapshot(self):
        rows = []
        for top, dirs, files in os.walk(self.base):
            dirs.sort()
            rel_top = os.path.relpath(top, self.base)
            for name in sorted(dirs + files):
                full = os.path.join(top, name)
                rel = os.path.normpath(os.path.join(rel_top, name))
                st = os.lstat(full)
                if stat.S_ISLNK(st.st_mode):
                    rows.append("l %s -> %s" % (rel, self.norm(os.readlink(full))))
                elif stat.S_ISDIR(st.st_mode):
                    rows.append("d %s/ %o" % (rel, stat.S_IMODE(st.st_mode)))
                elif not stat.S_ISREG(st.st_mode):
                    rows.append("o %s (special)" % rel)
                elif "/.nift/" in "/" + rel + "/" or rel.startswith(".nift"):
                    rows.append("f %s (nift metadata)" % rel)
                else:
                    with open(full, "rb") as handle:
                        data = handle.read()
                    if rel.startswith("w/"):
                        data = self.norm(data).encode("utf-8")
                    rows.append("f %s %o %d %s" % (rel, stat.S_IMODE(st.st_mode), len(data),
                                                   hashlib.sha256(data).hexdigest()[:16]))
        return "\n".join(rows) + "\n"

    def page_info(self, rel):
        path = os.path.join(self.w, ".nift", rel + ".info.json")
        if not os.path.exists(path):
            return {"missing": rel}
        with open(path, encoding="utf-8") as handle:
            info = json.load(handle)
        out = {}
        for k in DEP_FIELDS:
            if k in info:
                v = info[k]
                if k == "listings":
                    v = {key: "$FP" for key in v}
                elif k == "environment-snapshot":
                    v = "$FP"  # hashes the whole environment, which contains $BASE paths
                out[k] = json.loads(self.norm(json.dumps(v)))
        return out

    def cleanup(self):
        for top, dirs, _ in os.walk(self.base):
            for d in dirs:
                try:
                    os.chmod(os.path.join(top, d), 0o755)
                except OSError:
                    pass
        shutil.rmtree(self.base, ignore_errors=True)


def _drain(fd):
    chunks = []
    while True:
        data = os.read(fd, 65536)
        if not data:
            return b"".join(chunks)
        chunks.append(data)


def run_probe(probe, nift, parent):
    if wrapper(probe) is None:
        return None
    r = Run(probe, nift, parent)
    try:
        result = {}
        if probe.kind == "scripts":
            code, out, err, trace = r.nift_run(["main.f"] + probe.args, probe.stdin)
            result["exit"] = "%s\n" % code
            result["stdout"] = r.norm(out)
            result["stderr"] = r.norm(err)
            result["trace.jsonl"] = r.norm(trace, False)
        else:
            steps, traces, deps = [], [], {}
            for i, step in enumerate(probe.steps, 1):
                words = step.split(" ")
                op = words[0]
                if op == "nift":
                    code, out, err, trace = r.nift_run(words[1:])
                    steps.append("== step %d: %s\nexit %s\n-- stdout\n%s-- stderr\n%s" % (i, step, code, r.norm(out), r.norm(err)))
                    traces.append(json.dumps({"step": i, "cmd": step}) + "\n" + r.norm(trace, False))
                elif op in ("write", "append"):
                    text = step.split(" ", 2)[2].encode().decode("unicode_escape")
                    with open(os.path.join(r.w, words[1]), "w" if op == "write" else "a", encoding="utf-8") as handle:
                        handle.write(text)
                    steps.append("== step %d: %s\n" % (i, step))
                elif op == "remove":
                    os.remove(os.path.join(r.w, words[1]))
                    steps.append("== step %d: %s\n" % (i, step))
                elif op == "setenv":
                    r.env[words[1]] = words[2] if len(words) > 2 else ""
                    steps.append("== step %d: %s\n" % (i, step))
                elif op == "unsetenv":
                    r.env.pop(words[1], None)
                    steps.append("== step %d: %s\n" % (i, step))
                elif op == "info":
                    deps["step %d: %s" % (i, words[1])] = r.page_info(words[1])
                    steps.append("== step %d: %s\n" % (i, step))
                else:
                    raise SystemExit("%s: unknown step %r" % (probe.id, step))
            result["steps.txt"] = "".join(steps)
            result["trace.jsonl"] = "".join(traces)
            result["deps.json"] = json.dumps(deps, indent=1, sort_keys=True) + "\n"
        result["files.txt"] = r.snapshot()
        return result
    finally:
        r.cleanup()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--only", default="")
    ap.add_argument("--runs", type=int, default=2)
    ap.add_argument("--write", action="store_true", help="store the capture as expected/")
    ap.add_argument("--check", action="store_true", help="compare with expected/")
    ap.add_argument("--base-dir", default=os.environ.get("TMPDIR"))
    ap.add_argument("--mutant", default="", help="wrap nift in mutant.py MODE (use with --check)")
    a = ap.parse_args()
    global MUTANT
    MUTANT = a.mutant
    only = set(x for x in a.only.split(",") if x)
    failures = 0
    for probe in discover(only):
        results = [run_probe(probe, a.nift, a.base_dir) for _ in range(max(1, a.runs))]
        if results[0] is None:
            print("SKIPPED %s (needs Linux setpriv for priv=nodac)" % probe.id)
            continue
        unstable = sorted(k for k in results[0] if any(r[k] != results[0][k] for r in results[1:]))
        if unstable:
            failures += 1
            print("UNSTABLE %s: %s" % (probe.id, ", ".join(unstable)))
            continue
        result = results[0]
        target = os.path.join(EXPECTED, probe.id)
        if a.check:
            diffs = []
            for name, text in sorted(result.items()):
                path = os.path.join(target, name)
                want = open(path, encoding="utf-8", newline="").read() if os.path.exists(path) else None
                if want != text:
                    diffs.append(name)
                    sys.stdout.writelines(difflib.unified_diff((want or "").splitlines(True), text.splitlines(True),
                                                               "expected/%s/%s" % (probe.id, name), "candidate"))
            failures += bool(diffs)
            print(("DIFF %s: %s" % (probe.id, ", ".join(diffs))) if diffs else "OK %s" % probe.id)
        elif a.write:
            os.makedirs(target, exist_ok=True)
            for name, text in result.items():
                with open(os.path.join(target, name), "w", encoding="utf-8", newline="") as handle:
                    handle.write(text)
            print("WROTE %s" % probe.id)
        else:
            print("CAPTURED %s (%s)" % (probe.id, ", ".join(sorted(result))))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
