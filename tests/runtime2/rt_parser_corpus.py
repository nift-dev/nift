#!/usr/bin/env python3
"""CP10 corpus gates for the Runtime 2.0 script parser (src/rt).

Gates (docs/runtime-2/checkpoints.md CP10):
  1. accept: every corpus script the legacy translator accepts is accepted by
     rt::parse_script; every one it rejects is rejected with the identical
     message and line:column (legacy_oracle compare, in-process).
  2. error corpus: tests/rt/error_corpus.txt (mutants that legacy rejects at
     parse time) is reproduced exactly, and still matches legacy.
  3. paren oracle: each accepted script is reprinted with explicit
     parentheses around every operator expression (parse_dump reprint) and
     run with the legacy engine next to the original; stdout, exit status and
     stderr (columns and parentheses removed) must agree. Divergence means
     this parser's precedence differs from legacy's.
  4. fuzz: random byte/structure mutations never crash or hang the parser.

Usage: rt_parser_corpus.py --build .build --nift ./nift [--gen-count 5000]
       [--paren quick|full|none] [--fuzz-iters N] [--regen-errors N]
       [--record FILE.json]
"""
import argparse
import fnmatch
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ERROR_CORPUS = os.path.join(ROOT, "tests", "rt", "error_corpus.txt")


def find_f(base):
    out = []
    for dirpath, _, names in os.walk(base):
        out.extend(os.path.join(dirpath, n) for n in names if n.endswith(".f"))
    return sorted(out)


def collect(work, gen_count, seed):
    corpora = {
        "syntax-errors": find_f(os.path.join(ROOT, "tests/rt/syntax-errors")),
        "vm-corpus": find_f(os.path.join(ROOT, "tests/runtime2/vm-corpus")),
        "semantics": find_f(os.path.join(ROOT, "tests/runtime2/semantics")),
        "rt-diff-corpus": find_f(os.path.join(ROOT, "tests/runtime2/rt-diff-corpus")),
        "cp410-probes": find_f(os.path.join(ROOT, "docs/evidence/cp410-competitiveness/probes")),
        "tests-f": sorted(os.path.join(ROOT, "tests", n) for n in os.listdir(os.path.join(ROOT, "tests"))
                          if n.endswith(".f")),
    }
    smoke = os.path.join(work, "smoke")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/rt-diff/extract_smoke.py"),
                    "--tests", os.path.join(ROOT, "tests"), "--out", smoke], check=True, stdout=subprocess.DEVNULL)
    corpora["smoke-extract"] = find_f(smoke)
    gen = os.path.join(work, "gen")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/rt-diff/gen.py"), "--count", str(gen_count),
                    "--seed", str(seed), "--out", gen, "--sites", "0"], check=True, stdout=subprocess.DEVNULL)
    corpora["gen.py"] = find_f(gen)
    return corpora


def batched(items, size=400):
    for i in range(0, len(items), size):
        yield items[i:i + size]


def compare(oracle, files):
    stats = {"files": 0, "legacy_accepted": 0, "legacy_rejected_identical": 0, "mismatched": 0}
    rejects, mismatches = [], []
    for chunk in batched(files):
        r = subprocess.run([oracle, "compare"] + chunk, capture_output=True, text=True, errors="replace")
        lines = r.stdout.splitlines()
        for i, line in enumerate(lines):
            if line.startswith("compare:"):
                for k, v in re.findall(r"(\w+)=(\d+)", line):
                    stats[k] += int(v)
            elif line.startswith("REJECT"):
                rejects.append(line.split("\t", 1)[1])
            elif line.startswith("MISMATCH"):
                mismatches.append("\n".join(lines[i:i + 3]))
        if r.returncode not in (0, 1):
            mismatches.append("oracle crashed (rc=%d) on batch starting %s: %s" % (r.returncode, chunk[0], r.stderr))
    return stats, rejects, mismatches


def opaque_stats(dump, files):
    totals = {}
    for chunk in batched(files):
        r = subprocess.run([dump, "check"] + chunk, capture_output=True, text=True, errors="replace")
        for k, v in re.findall(r"([\w-]+)=(\d+)", r.stderr):
            if k in ("files", "accepted"):
                continue
            totals[k] = totals.get(k, 0) + int(v)
        for line in r.stdout.splitlines():
            parts = line.split("\t")
            if len(parts) > 2:
                totals["units_with_opaque"] = totals.get("units_with_opaque", 0) + 1
    return totals


NORMALISE_POS = re.compile(r"(\.f):(\d+):\d+")


def run_nift(nift, workdir, script):
    env = {"PATH": os.environ.get("PATH", ""), "HOME": os.path.join(workdir, ".home"), "LANG": "C", "LC_ALL": "C",
           "TZ": "UTC", "TMPDIR": os.path.join(workdir, ".tmp")}
    os.makedirs(env["HOME"], exist_ok=True)
    os.makedirs(env["TMPDIR"], exist_ok=True)
    try:
        r = subprocess.run([nift, script], cwd=workdir, env=env, capture_output=True, stdin=subprocess.DEVNULL,
                           timeout=20)
    except subprocess.TimeoutExpired:
        return (b"", b"timeout", -9)
    except OSError as e:
        return (b"", str(e).encode(), -1)
    real = os.path.realpath(workdir).encode()
    clean = lambda s: s.replace(real, b"$ROOT").replace(workdir.encode(), b"$ROOT")
    return clean(r.stdout), clean(r.stderr), r.returncode


def stage(src_script, dest):
    parent = os.path.dirname(src_script)
    names = os.listdir(parent)
    size = sum(os.path.getsize(os.path.join(parent, n)) for n in names if os.path.isfile(os.path.join(parent, n)))
    if len(names) < 200 and size < 5_000_000 and parent != os.path.join(ROOT, "tests"):
        shutil.copytree(parent, dest, symlinks=True,
                        ignore=shutil.ignore_patterns("*.stdout", "*.stderr", "*.exit"))
    else:
        os.makedirs(dest)
        shutil.copy(src_script, dest)
    return os.path.join(dest, os.path.basename(src_script))


def paren_one(args):
    dump, nift, script, work = args
    base = tempfile.mkdtemp(dir=work)
    try:
        orig = stage(script, os.path.join(base, "o"))
        repr_ = stage(script, os.path.join(base, "r"))
        # Reprint every .f in the staged copy (modules included).
        parens = 0
        for f in find_f(os.path.dirname(repr_)):
            r = subprocess.run([dump, "reprint", f], capture_output=True)
            if r.returncode != 0:
                continue
            with open(f, "rb") as before:
                parens += r.stdout.count(b"(") - before.read().count(b"(")
            with open(f, "wb") as out:
                out.write(r.stdout)
        a = run_nift(nift, os.path.dirname(orig), os.path.basename(orig))
        b = run_nift(nift, os.path.dirname(repr_), os.path.basename(repr_))
        strip = lambda s: NORMALISE_POS.sub(r"\1:\2", s.decode("utf-8", "replace")).replace("(", "").replace(")", "")
        if a[0] == b[0] and a[2] == b[2] and strip(a[1]) == strip(b[1]):
            return {"same": True, "parens": parens, "exit0": a[2] == 0}
        return {"same": False, "parens": parens, "script": os.path.relpath(script, ROOT) if script.startswith(ROOT) else script,
                "orig": [a[0].decode("utf-8", "replace")[-400:], a[1].decode("utf-8", "replace")[-400:], a[2]],
                "reprint": [b[0].decode("utf-8", "replace")[-400:], b[1].decode("utf-8", "replace")[-400:], b[2]]}
    finally:
        shutil.rmtree(base, ignore_errors=True)


def load_ignore():
    """Entries tools/rt-diff/ignore.txt marks nondeterministic (clock, thread
    interleaving, endless loops): the same binary twice differs on them."""
    patterns = []
    for line in open(os.path.join(ROOT, "tools/rt-diff/ignore.txt")):
        line = line.split("#", 1)[0].strip()
        if line:
            patterns.append(line.split()[0])
    return patterns


def paren_oracle(dump, nift, files, work, jobs):
    patterns = load_ignore()
    entry = lambda f: os.path.relpath(f, work).replace(os.sep, "/")
    skipped = [f for f in files if f.startswith(work) and any(fnmatch.fnmatch(entry(f), p) for p in patterns)]
    files = [f for f in files if f not in skipped]
    print("paren oracle: skipped %d nondeterministic entries (tools/rt-diff/ignore.txt)" % len(skipped))
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        results = list(pool.map(paren_one, [(dump, nift, f, work) for f in files]))
    stats = {"skipped_nondeterministic": len(skipped), "inserted_parentheses": sum(r["parens"] for r in results),
             "scripts_with_insertions": sum(1 for r in results if r["parens"]),
             "same_and_exit0": sum(1 for r in results if r["same"] and r["exit0"])}
    return [r for r in results if not r["same"]], stats


def run_limited(argv, seconds):
    """Run with a wall-clock limit (a hang is a failure, rc 124)."""
    try:
        return subprocess.run(argv, capture_output=True, text=True, errors="replace", timeout=seconds)
    except subprocess.TimeoutExpired as e:
        out = e.stdout.decode("utf-8", "replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
        return subprocess.CompletedProcess(argv, 124, out, "timeout after %ds" % seconds)


def executable(build, name):
    path = os.path.abspath(os.path.join(build, name))
    return path + ".exe" if not os.path.exists(path) and os.path.exists(path + ".exe") else path


def sanitized_run(dump, a):
    work = tempfile.mkdtemp(prefix="rt-parser-asan.")
    failed = []
    record = {}
    try:
        corpora = collect(work, a.gen_count, a.seed)
        files = [f for fs in corpora.values() for f in fs]
        crashed = 0
        for chunk in batched(files):
            r = subprocess.run([dump, "check"] + chunk, capture_output=True, text=True, errors="replace")
            if r.returncode != 0 or "runtime error" in r.stderr or "AddressSanitizer" in r.stderr:
                crashed += 1
                print(r.stderr[-3000:])
        record["check"] = {"files": len(files), "failed_batches": crashed}
        print("sanitized check: files=%d failed_batches=%d" % (len(files), crashed))
        if crashed:
            failed.append("sanitized corpus check")
        r = subprocess.run([dump, "errors", ERROR_CORPUS], capture_output=True, text=True, errors="replace")
        print(r.stdout.splitlines()[-1] if r.stdout else r.stderr[-3000:])
        if r.returncode or "runtime error" in r.stderr:
            failed.append("sanitized error corpus")
        seeds = corpora["vm-corpus"] + corpora["semantics"] + corpora["gen.py"][:200]
        r = run_limited([dump, "fuzz", str(a.seed), str(a.fuzz_iters)] + seeds, 3000)
        print(r.stdout.strip() or r.stderr[-3000:])
        record["fuzz"] = r.stdout.strip()
        if r.returncode or "runtime error" in r.stderr:
            print(r.stderr[-3000:])
            failed.append("sanitized fuzz (rc=%d)" % r.returncode)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if a.record:
        with open(a.record, "w") as out:
            json.dump({"sanitized": record, "failed": failed}, out, indent=1, sort_keys=True)
    print(("FAIL rt parser sanitized: " + "; ".join(failed)) if failed else "PASS rt parser sanitized")
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default=".build")
    ap.add_argument("--nift", default="./nift")
    ap.add_argument("--gen-count", type=int, default=5000)
    ap.add_argument("--seed", type=int, default=1010)
    ap.add_argument("--paren", choices=["none", "quick", "full"], default="quick")
    ap.add_argument("--fuzz-iters", type=int, default=20000)
    ap.add_argument("--regen-errors", type=int, default=0, help="rebuild tests/rt/error_corpus.txt with N cases")
    ap.add_argument("--record", help="write the gate counts as JSON")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--sanitized", help="ASan/UBSan parse_dump: corpus check, error corpus and fuzz only")
    a = ap.parse_args()
    if a.sanitized:
        return sanitized_run(os.path.abspath(a.sanitized), a)
    oracle, dump = executable(a.build, "rt-legacy-oracle"), executable(a.build, "rt-parse-dump")
    nift = os.path.abspath(a.nift)
    if not os.path.exists(nift) and os.path.exists(nift + ".exe"):
        nift += ".exe"
    work = tempfile.mkdtemp(prefix="rt-parser-corpus.")
    record = {"corpora": {}, "gates": {}}
    failed = []
    try:
        corpora = collect(work, a.gen_count, a.seed)
        all_files = []
        for name, files in corpora.items():
            t0 = time.time()
            stats, rejects, mismatches = compare(oracle, files)
            stats["opaque"] = opaque_stats(dump, files)
            stats["seconds"] = round(time.time() - t0, 2)
            stats["legacy_rejects"] = [os.path.relpath(r, work) if r.startswith(work) else os.path.relpath(r, ROOT)
                                       for r in rejects]
            record["corpora"][name] = stats
            print("%-15s files=%-5d legacy_accepted=%-5d rejected_identical=%-3d mismatched=%d" % (
                name, stats["files"], stats["legacy_accepted"], stats["legacy_rejected_identical"],
                stats["mismatched"]))
            for m in mismatches:
                print(m)
            if mismatches:
                failed.append("accept/reject gate: %s (%d mismatches)" % (name, len(mismatches)))
            all_files.extend(files)

        if a.regen_errors:
            seeds = [f for f in all_files if os.path.getsize(f) <= 1536]
            r = subprocess.run([oracle, "mutate", str(a.seed), str(a.regen_errors), ERROR_CORPUS] + seeds,
                               capture_output=True, text=True, errors="replace")
            print(r.stdout[-6000:])
            if r.returncode:
                failed.append("error corpus regeneration found mismatches")
        r = subprocess.run([dump, "errors", ERROR_CORPUS], capture_output=True, text=True, errors="replace")
        print(r.stdout.splitlines()[-1] if r.stdout else r.stderr)
        if r.returncode:
            print(r.stdout[-4000:])
            failed.append("error corpus (rt)")
        r2 = subprocess.run([oracle, "errors", ERROR_CORPUS], capture_output=True, text=True, errors="replace")
        print("legacy " + (r2.stdout.splitlines()[-1] if r2.stdout else r2.stderr))
        if r2.returncode:
            print(r2.stdout[-4000:])
            failed.append("error corpus (legacy drift)")
        record["gates"]["error_corpus"] = r.stdout.splitlines()[-1] if r.stdout else ""

        fuzz_seeds = [f for f in corpora["vm-corpus"] + corpora["semantics"] + corpora["gen.py"][:200]]
        t0 = time.time()
        r = run_limited([dump, "fuzz", str(a.seed), str(a.fuzz_iters)] + fuzz_seeds, 1800)
        print(r.stdout.strip() or r.stderr.strip())
        record["gates"]["fuzz"] = {"output": r.stdout.strip(), "rc": r.returncode,
                                   "seconds": round(time.time() - t0, 1)}
        if r.returncode:
            failed.append("fuzz (rc=%d)" % r.returncode)

        if a.paren != "none":
            names = ["vm-corpus", "semantics", "rt-diff-corpus", "cp410-probes", "tests-f"]
            if a.paren == "full":
                names += ["smoke-extract", "gen.py"]
            files = [f for n in names for f in corpora[n]]
            t0 = time.time()
            divergent, stats = paren_oracle(dump, nift, files, work, a.jobs)
            skipped = stats["skipped_nondeterministic"]
            record["gates"]["paren_oracle"] = dict(stats, scripts=len(files) - skipped, divergent=len(divergent),
                                                   seconds=round(time.time() - t0, 1), details=divergent)
            print("paren oracle: scripts=%d divergent=%d scripts_with_insertions=%d inserted_parentheses=%d "
                  "same_and_exit0=%d" % (len(files) - skipped, len(divergent), stats["scripts_with_insertions"],
                                         stats["inserted_parentheses"], stats["same_and_exit0"]))
            for d in divergent:
                print(json.dumps(d)[:1500])
            if divergent:
                failed.append("paren oracle (%d divergent)" % len(divergent))
    finally:
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print("kept", work)
    record["failed"] = failed
    if a.record:
        with open(a.record, "w") as out:
            json.dump(record, out, indent=1, sort_keys=True)
    if failed:
        print("FAIL rt parser corpus: " + "; ".join(failed))
        return 1
    print("PASS rt parser corpus")
    return 0


if __name__ == "__main__":
    sys.exit(main())
