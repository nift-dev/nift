#!/usr/bin/env python3
"""CP91 scripting performance campaign: nift run / nift sh / scripting surfaces.

Times the scripting hosts rather than template builds (CP47 covered those).
Each workload is timed over repeated samples (cold process per sample).
Reports min/p25/median/p75/max, mean, stddev and raw samples.

Surfaces: nift run startup, statement throughput, named-fn and lambda
invocation, imports, filesystem primitives, whole-file open, ifs/ofs
streaming and read_val, and nift sh piped-statement throughput.
"""
import pathlib, shutil, statistics, subprocess, tempfile, time, json, os

NIFT = os.environ.get("NIFT_BIN")
assert NIFT, "NIFT_BIN required"


def sample(fn, n=15):
    vals = []
    for _ in range(n):
        t0 = time.perf_counter()
        fn()
        vals.append((time.perf_counter() - t0) * 1000.0)
    vals.sort()
    med = statistics.median(vals)
    return {
        "min": round(vals[0], 3), "p25": round(vals[len(vals) // 4], 3),
        "median": round(med, 3), "p75": round(vals[(3 * len(vals)) // 4], 3),
        "max": round(vals[-1], 3), "mean": round(statistics.mean(vals), 3),
        "stddev": round(statistics.pstdev(vals), 3), "samples": len(vals),
    }


def write(path, text):
    path.write_text(text)


def main():
    root = pathlib.Path(tempfile.mkdtemp(prefix="cp91-"))
    nift = pathlib.Path(NIFT)
    results = {}

    # nift run startup (empty script).
    empty = root / "empty.nift"; write(empty, "\n")
    results["run_startup_empty"] = sample(lambda: subprocess.run([nift, "run", str(empty)], capture_output=True))

    # Statement throughput: 2000 simple assignments + arithmetic.
    stmts = "\n".join("x%d := %d\nx%d += 1" % (i, i, i) for i in range(1000))
    s = root / "stmts.nift"; write(s, stmts + "\nprint(0)\n")
    results["statements_2k"] = sample(lambda: subprocess.run([nift, "run", str(s)], capture_output=True))

    # Named function invocation: 20k calls to a recursive/iterative fn.
    fnbody = "fn(acc(n)) { r := 0\nwhile(n > 0) { r += n\nn -= 1 }\nreturn r }\n" + "print(acc(200))\n" * 100
    f = root / "fn.nift"; write(f, fnbody)
    results["named_fn_100_calls"] = sample(lambda: subprocess.run([nift, "run", str(f)], capture_output=True))

    # Lambda invocation.
    lb = "lam := (n) => { r := 0\nwhile(n > 0) { r += n\nn -= 1 }\nreturn r }\n" + "print(lam(200))\n" * 100
    l = root / "lam.nift"; write(l, lb)
    results["lambda_100_calls"] = sample(lambda: subprocess.run([nift, "run", str(l)], capture_output=True))

    # Import: a library imported once per run (plus a tiny body).
    lib = root / "lib.nift"
    write(lib, "base := 10\nmul := (x) => x * base\nexport(mul)\n")
    imp = root / "imp.nift"
    write(imp, "@import(\"lib.nift\")\nprint(mul(2))\n")
    results["import_once"] = sample(lambda: subprocess.run([nift, "run", str(imp)], capture_output=True))

    # Filesystem primitives + whole-file open.
    fs = root / "fs.nift"
    write(fs, "for(i : [1,2,3,4,5,6,7,8,9,10]) { touch(\"f\" + i) }\nprint(open(\"f5\") == \"\")\n")
    results["fs_10_touch_open"] = sample(lambda: subprocess.run([nift, "run", str(fs)], capture_output=True))

    # ifs/ofs streaming: write 200 lines then read them all back.
    big = root / "data.txt"; write(big, "".join("line-%d\n" % i for i in range(5000)))
    st = root / "stream.nift"
    write(st, "i := ifs(\"data.txt\")\ncount := 0\nline := i.read_line()\nwhile(line != null) { count += 1\nline = i.read_line() }\nprint(count)\nclose(i)\n")
    results["stream_read_5k_lines"] = sample(lambda: subprocess.run([nift, "run", str(st)], capture_output=True))

    # read_val: parse 500 values.
    vals = " ".join("true %d %d.5 \"s%d\"" % (i, i, i) for i in range(125))
    vf = root / "vals.txt"; write(vf, vals)
    rv = root / "readval.nift"
    write(rv, "v := ifs(\"vals.txt\")\nn := 0\nval := v.read_val()\nwhile(val != null) { n += 1\nval = v.read_val() }\nprint(n)\nclose(v)\n")
    results["read_val_500"] = sample(lambda: subprocess.run([nift, "run", str(rv)], capture_output=True))

    # nift sh piped statement throughput: 500 REPL statements in one session.
    repl_cmds = "\n".join("y%d := %d" % (i, i) for i in range(500)) + "\nquit\n"
    results["repl_500_statements"] = sample(
        lambda: subprocess.run([nift, "sh"], input=repl_cmds, capture_output=True, text=True))

    print(json.dumps(results, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()