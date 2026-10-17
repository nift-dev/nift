#!/usr/bin/env python3
"""A deliberately wrong `nift` for rt-diff self-tests (CP08).

Usage: rt_diff_mutant.py MODE REAL_NIFT [nift args...]

Runs the real binary, then applies exactly one mutation MODE to what rt-diff
observes (stdout, stderr, exit code, files, trace). Used by
tests/runtime2/rt_diff_selftest.sh to prove that each comparison channel and
each normalisation rule catches a real difference and hides nothing else.
"""
import json
import os
import subprocess
import sys


def main():
    mode, real, args = sys.argv[1], sys.argv[2], sys.argv[3:]
    env = dict(os.environ)
    stdin_data = sys.stdin.buffer.read() if not sys.stdin.isatty() else b""
    # rt-diff's combined run puts stdout and stderr on one pipe; the real
    # binary then writes to that same pipe so interleaving is preserved
    # (except in `interleave` mode, whose whole point is to change it).
    combined = os.fstat(1).st_ino == os.fstat(2).st_ino and mode != "interleave"
    trace_fd = env.get("NIFT_TRACE_HOST")
    trace_tmp = None
    if trace_fd is not None:
        # Interpose on the trace: the real binary writes to our file, we
        # forward (possibly mutated) lines to the harness's descriptor.
        trace_tmp = os.path.join(env.get("TMPDIR", "/tmp"), ".mutant-trace-%d" % os.getpid())
        fd = os.open(trace_tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        env["NIFT_TRACE_HOST"] = str(fd)
        proc = subprocess.run([real] + args, env=env, input=stdin_data, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT if combined else subprocess.PIPE, pass_fds=(fd,))
        os.close(fd)
    else:
        proc = subprocess.run([real] + args, env=env, input=stdin_data, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT if combined else subprocess.PIPE)
    out, err, code = proc.stdout, proc.stderr or b"", proc.returncode
    trace = b""
    if trace_tmp:
        with open(trace_tmp, "rb") as handle:
            trace = handle.read()
        os.remove(trace_tmp)
    cwd = os.getcwd()

    def overwrite(path, data):
        # Page-info files are read-only (0444); as a non-root user (CI) a plain
        # open(..., "wb") fails. Keep the original mode afterwards.
        mode = os.stat(path).st_mode
        os.chmod(path, mode | 0o200)
        with open(path, "wb") as handle:
            handle.write(data)
        os.chmod(path, mode)

    def walk_files(top):
        for d, _, names in os.walk(top):
            for n in names:
                yield os.path.join(d, n)

    if mode == "identity":
        pass
    elif mode == "file-mode":
        for path in walk_files(os.path.join(cwd, "public")):
            os.chmod(path, 0o600)
    elif mode == "empty-dir":
        os.makedirs(os.path.join(cwd, "public" if os.path.isdir(os.path.join(cwd, "public")) else ".", "rt-mutant-empty"), exist_ok=True)
    elif mode == "nift-garble":
        for path in walk_files(os.path.join(cwd, ".nift")):
            if path.endswith(".info.json"):
                with open(path, "rb") as handle:
                    data = handle.read()
                overwrite(path, data.replace(b'"reqs"', b'"reqs" ', 1).replace(b"templates/", b"Templates/", 1))
    elif mode == "home-write":
        with open(os.path.join(env["HOME"], "rt-mutant.txt"), "w") as handle:
            handle.write("x")
    elif mode == "tmp-write":
        with open(os.path.join(env["TMPDIR"], "rt-mutant.txt"), "w") as handle:
            handle.write("x")
    elif mode == "home-tmp-swap":
        out = out.replace(env["HOME"].encode(), env["TMPDIR"].encode())
    elif mode == "stdout-only":
        if out:
            out += b"x\n"
    elif mode == "stderr-only":
        if err:
            err += b"x\n"
    elif mode == "exit-only":
        if code != 0:
            code += 1
    elif mode == "stdin-only":
        if stdin_data:
            out += b"x\n"
    elif mode == "eval-key-order":
        if args[:1] == ["eval"]:
            try:
                pairs = json.loads(out, object_pairs_hook=list)
            except ValueError:
                pairs = None
            if isinstance(pairs, list) and pairs and all(isinstance(p, tuple) for p in pairs) and len(pairs) > 1:
                body = ",\n".join("%s: %s" % (json.dumps(k), json.dumps(v)) for k, v in reversed(pairs))
                out = ("{\n" + body + "\n}\n").encode()
    elif mode == "number-format":
        if args[:1] == ["eval"]:
            out = out.replace(b"2.5\n", b"2.50\n")
    elif mode == "time-number":
        # Only the number in `time taken: N seconds` changes: must stay hidden.
        import re
        out = re.sub(rb"time taken: [0-9.eE+-]+ seconds", b"time taken: 99.5 seconds", out)
    elif mode == "time-word":
        out = out.replace(b"time taken:", b"time used:")
    elif mode == "interleave":
        # Same bytes per stream, different relative order on a shared pipe.
        if os.fstat(1).st_ino == os.fstat(2).st_ino:
            sys.stdout.buffer.write(err + out)
            sys.stdout.flush()
            sys.exit(code)
    elif mode.startswith("trace-"):
        lines = trace.splitlines(keepends=True)
        if mode == "trace-drop" and lines:
            lines = lines[:-1]
        elif mode == "trace-reorder":
            for i in range(len(lines) - 1):
                a, b = json.loads(lines[i]), json.loads(lines[i + 1])
                if a["ctx"] == b["ctx"] and lines[i] != lines[i + 1]:
                    lines[i], lines[i + 1] = lines[i + 1], lines[i]
                    break
        elif mode == "trace-async-reorder":
            # Swap the first two distinct operations of one async task. They
            # need not be adjacent: on a multi-core runner other tasks' and the
            # parent's operations interleave between them (CI @bce5913 missed
            # the reorder because no adjacent pair existed there).
            seen = {}
            for i, line in enumerate(lines):
                ctx = json.loads(line)["ctx"]
                if "/async#" not in ctx:
                    continue
                j = seen.get(ctx)
                if j is not None and lines[j] != line:
                    lines[i], lines[j] = lines[j], lines[i]
                    break
                seen.setdefault(ctx, i)
        elif mode.startswith("trace-drop-op:"):
            op = mode.split(":", 1)[1]
            lines = [l for l in lines if json.loads(l).get("op") != op]
        trace = b"".join(lines)
    elif mode == "r2-goldens":
        # Emulates an engine that implements the D-009 decisions: for a
        # semantics probe with intended (.r2.*) goldens, emit those instead.
        script = args[0] if args else ""
        base = os.path.join(os.environ.get("RT_MUTANT_R2_DIR", ""), os.path.splitext(os.path.basename(script))[0])
        if script.endswith(".f") and os.path.isfile(base + ".r2.stdout"):
            root = os.getcwd()
            def golden(suffix):
                with open(base + suffix, "rb") as handle:
                    return handle.read().replace(b"$ROOT", root.encode())
            if os.fstat(1).st_ino == os.fstat(2).st_ino:
                sys.stdout.buffer.write(golden(".r2.stdout") + golden(".r2.stderr"))
            else:
                sys.stdout.buffer.write(golden(".r2.stdout"))
                sys.stderr.buffer.write(golden(".r2.stderr"))
            if trace_fd is not None:
                os.write(int(trace_fd), trace)
            sys.exit(int(golden(".r2.exit")))
    elif mode.startswith("x-"):
        import re, glob
        infos = [p for p in walk_files(os.path.join(cwd, ".nift")) if p.endswith(".info.json")] if os.path.isdir(os.path.join(cwd, ".nift")) else []
        def rewrite(fn):
            for p in infos:
                d = open(p, "rb").read(); n = fn(d)
                if n != d: overwrite(p, n)
        if mode == "x-racy-drop":
            rewrite(lambda d: re.sub(rb',\n  "racy-dependencies": \[[^\]]*\]', b"", d))
        elif mode == "x-tick-drop":
            rewrite(lambda d: re.sub(rb',\n  "tick-hashes": \{[^}]*\}', b"", d))
        elif mode == "x-listing-garbage":
            def g(d):
                try: j = json.loads(d)
                except ValueError: return d
                if isinstance(j.get("listings"), dict) and j["listings"]:
                    j["listings"] = {k: "0" for k in j["listings"]}
                    return json.dumps(j, indent=2).encode()
                return d
            rewrite(g)
        elif mode == "x-info-reformat":
            rewrite(lambda d: d.replace(b"\n  ", b"\n\t"))
        elif mode == "x-info-escape":
            rewrite(lambda d: d.replace(b"/", b"\\/"))
        elif mode == "x-info-dupkey":
            rewrite(lambda d: d.replace(b'"title": ', b'"title": "BOGUS",\n  "title": ', 1))
        elif mode == "x-info-number":
            rewrite(lambda d: re.sub(rb'("pagination-pages": )(\d+)', rb'\g<1>\g<2>.0', d))
        elif mode == "x-escape-write":
            # A write that escapes the working copy through `..`.
            open(os.path.join(cwd, "..", "rt-escape.txt"), "a").write("x")
        elif mode == "x-hang":
            # Never finishes: rt-diff must kill the whole process group and
            # report a timeout difference.
            import time
            child = subprocess.Popen(["sleep", "300"])
            time.sleep(300)
        elif mode == "x-time-user":
            out = out.replace(b"time taken: 1 seconds", b"time taken: 2 seconds")
        elif mode == "x-trace-extra-field":
            trace = b"".join(l.rstrip(b"}\n") + b',"extra":1}\n' for l in trace.splitlines(keepends=True))
        elif mode == "x-trace-cross":
            ls_ = trace.splitlines(keepends=True)
            kids = [l for l in ls_ if b'/async#' in l or b'/thread#' in l]
            trace = b"".join([l for l in ls_ if l not in kids] + kids)
        elif mode == "x-trace-page-cross":
            ls_ = trace.splitlines(keepends=True)
            trace = b"".join(sorted(ls_, key=lambda l: json.loads(l)["ctx"]))
        elif mode == "x-trace-page-reverse":
            # Pages built in the opposite order (each page's own operations
            # unchanged): visible only through cross-page order.
            groups, order = {}, []
            for l in trace.splitlines(keepends=True):
                c = json.loads(l)["ctx"]
                if c not in groups: order.append(c); groups[c] = []
                groups[c].append(l)
            trace = b"".join(l for c in reversed(order) for l in groups[c])
        elif mode == "x-trace-relabel":
            trace = trace.replace(b'"ctx":"script/async#1"', b'"ctx":"script/async#9"')
        elif mode == "x-racy-inject":
            if args[:1] == ["build"]:
                rewrite(lambda d: d.replace(b'\n  "dependencies": [', b'\n  "racy-dependencies": ["templates/template.html"],\n  "dependencies": [', 1))
        elif mode == "x-tick-inject":
            if args[:1] == ["build"]:
                rewrite(lambda d: d.replace(b'\n  "dependencies": [', b'\n  "tick-hashes": {"templates/template.html": "1"},\n  "dependencies": [', 1))
        elif mode == "x-replace-plus":
            out = out.replace(b"item", b"ITEM") + b"extra\n"
        elif mode == "x-empty-old":
            out = b"".join(bytes([c]) + b"Z" for c in out); out = b"Z" + out
        else:
            sys.exit("unknown mutant mode %s" % mode)
    elif mode.startswith("replace:"):
        # replace:OLD:NEW — used by policy self-tests.
        _, old, new = mode.split(":", 2)
        out = out.replace(old.encode(), new.encode())
    else:
        sys.exit("unknown mutant mode %s" % mode)
    if trace_fd is not None:
        os.write(int(trace_fd), trace)
    sys.stdout.buffer.write(out)
    sys.stdout.flush()
    sys.stderr.buffer.write(err)
    sys.stderr.flush()
    sys.exit(code)


if __name__ == "__main__":
    main()
