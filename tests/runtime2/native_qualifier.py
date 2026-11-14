#!/usr/bin/env python3
"""L6 `native::` (D-042, D-044 #2, D-075 #2, D-095; checkpoints/native-qualifier.md), both engines.

  native_qualifier.py --nift NIFT          every case, VM (NIFT_VM=require) and legacy (NIFT_VM=0)
  native_qualifier.py --embed              builds and runs the embedding-API check (harness toolchain env)
  native_qualifier.py --nift N --mutants   the production binary N passes every case (no-mutation control), then
                                           NIFT_L6_MUTANT=1..6, each built with sanctioned_mutant.py, must be caught

Contract: `native::<name>(` (nothing around `::`, the `(` straight after) calls the native of exactly cat, cp, mv,
rm, ls, mkdir, touch in both engines, with every check the unqualified native had. In VM-default execution the
unqualified seven are no built-ins: a user function of that name is ordinary; otherwise the whole program is refused
(`native-unqualified`), reached or not, before anything runs. Legacy keeps them (the historical reference). A
function call never launches an external program: every run has a scratch PATH first whose cat, cp, ... would
leave a marker. Every other spelling, and a name outside the seven, is reported before anything runs, with the same
text and position in both engines. Fixtures are fresh temporary directories; nothing outside them is written, and
/dev/stdout is never opened. Exit status: 0 = every case passes (and every mutant is caught); 1 = a failure.
"""
import argparse
import os
import resource
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SEVEN = ["cat", "cp", "mv", "rm", "ls", "mkdir", "touch"]
UNKNOWN = " is not a native function (native:: names cat, cp, mv, rm, ls, mkdir and touch)"
MALFORMED = "malformed native:: call: write native::name(...)"
REFUSED = "not compiled for the VM (native-unqualified)"
CALLS = {  # the seven, qualified; each check reads the effect in the fixture directory
    "cat": ('native::cat("a.txt")', lambda d, out: out == "A"),
    "cp": ('native::cp("a.txt", "b.txt")', lambda d, out: read(d, "b.txt") == "A"),
    "mv": ('native::mv("a.txt", "m.txt")', lambda d, out: read(d, "m.txt") == "A" and read(d, "a.txt") is None),
    "rm": ('native::rm("a.txt")', lambda d, out: read(d, "a.txt") is None),
    "ls": ('print(native::ls(".").length() > 0)', lambda d, out: out == "true\n"),
    "mkdir": ('native::mkdir("dd")', lambda d, out: os.path.isdir(os.path.join(d, "dd"))),
    "touch": ('native::touch("t")', lambda d, out: read(d, "t") == ""),
}


def read(d, n):
    try:
        with open(os.path.join(d, n)) as f:
            return f.read()
    except OSError:
        return None


def limits():
    resource.setrlimit(resource.RLIMIT_AS, (2 << 30, 2 << 30))
    resource.setrlimit(resource.RLIMIT_FSIZE, (64 << 20, 64 << 20))


def fixture(files=None):
    """A fresh directory with a.txt and the fake PATH: fakebin/<name> leaves LAUNCHED-<name> if ever run."""
    d = tempfile.mkdtemp(prefix="l6-")
    os.makedirs(os.path.join(d, "fakebin"))
    for n in SEVEN:
        p = os.path.join(d, "fakebin", n)
        with open(p, "w") as f:
            f.write('#!/bin/sh\n: > "%s/LAUNCHED-%s"\n' % (d, n))
        os.chmod(p, 0o755)
    for n, text in dict({"a.txt": "A"}, **(files or {})).items():
        os.makedirs(os.path.dirname(os.path.join(d, n)), exist_ok=True)
        with open(os.path.join(d, n), "w") as f:
            f.write(text)
    return d


def run(nift, d, src, vm, args=None, stdin=None, extra=None, cwd=None):
    if src is not None:
        with open(os.path.join(d, "main.f"), "w") as f:
            f.write(src)
    env = {"PATH": os.path.join(d, "fakebin") + ":/usr/bin:/bin", "HOME": d, "LANG": "C",
           "NIFT_VM": "require" if vm else "0", **(extra or {})}
    env = {k: v for k, v in env.items() if v is not None}  # None: unset (the VM default)
    try:
        p = subprocess.run([nift] + (args or ["main.f"]), cwd=cwd or d, env=env, input=stdin, capture_output=True,
                           text=True, timeout=60, preexec_fn=limits)
    except subprocess.TimeoutExpired:
        return None, "", "timeout"
    return p.returncode, p.stdout, p.stderr.replace(d + "/", "")


def launched(d):
    out = []
    for dp, _, fs in os.walk(d):
        out += [f for f in fs if f.startswith("LAUNCHED-")]
    return out


def ok(cond, problem):
    return [] if cond else [problem]


def show(rc, out, err):
    return "rc %s out %r err %r" % (rc, out[-160:], err[-300:])


# ---- the cases: name -> function(nift, vm) returning a list of problems -----------------------------------------
CASES = {}


def case(name):
    def register(f):
        CASES[name] = f
        return f
    return register


for _n in SEVEN:
    def _qualified(nift, vm, n=_n):
        d = fixture()
        try:
            src, check = CALLS[n]
            rc, out, err = run(nift, d, src + "\n", vm)
            return ok(rc == 0 and check(d, out), show(rc, out, err)) + ok(not launched(d), "launched %s" % launched(d))
        finally:
            shutil.rmtree(d, ignore_errors=True)
    CASES["qualified-" + _n] = _qualified

    # VM: the unqualified name refused, wherever it stands, before the first statement runs; legacy: the native.
    for _where, _wrap in (("top", "%s"), ("unreached", "if (false) { %s }"), ("function", "fn(g()) { %s }"),
                          ("lambda", "h := () => { %s }"), ("async", "fn[async](w()) { %s; return 1 }")):
        def _unqualified(nift, vm, n=_n, wrap=_wrap):
            d = fixture()
            try:
                call = 'x := ls(".")' if n == "ls" else CALLS[n][0].replace("native::", "")
                src = 'print("first")\n' + wrap % call + "\n"
                rc, out, err = run(nift, d, src, vm)
                problems = ok(not launched(d), "launched %s" % launched(d))
                if vm:
                    return problems + ok(rc == 1 and REFUSED in err and "native::name(...)" in err and not out,
                                         show(rc, out, err)) + ok(read(d, "a.txt") == "A", "an effect ran")
                # legacy is unchanged: the unqualified native runs (its effect checked at the top level)
                effect = wrap != "%s" or CALLS[n][1](d, out[len("first\n"):]) or n == "ls"
                return problems + ok(rc == 0 and out.startswith("first") and effect, show(rc, out, err))
            finally:
                shutil.rmtree(d, ignore_errors=True)
        CASES["unqualified-%s-%s" % (_n, _where)] = _unqualified

    # A user function of the name: ordinary in the VM (and native::<name> is still the native); legacy's native
    # heads win over user functions (the historical reference), recorded, not required.
    def _user(nift, vm, n=_n):
        d = fixture()
        try:
            src = 'fn(%s(x, y)) { print("user " + x) }\n%s("z", "w")\n%s\n' % (n, n, CALLS[n][0])
            rc, out, err = run(nift, d, src, vm)
            if not vm:
                return ok(not launched(d), "launched %s" % launched(d))
            return ok(rc == 0 and out.startswith("user z") and CALLS[n][1](d, out[len("user z\n"):]),
                      show(rc, out, err)) + ok(not launched(d), "launched %s" % launched(d))
        finally:
            shutil.rmtree(d, ignore_errors=True)
    CASES["user-function-" + _n] = _user

    # D-096 3a: with a user function of the name, the unqualified call (reached, and in an unreachable branch) is the
    # user function under the VM default, never the native-unqualified refusal. Legacy's native head wins (recorded).
    def _user_vm_default(nift, vm, n=_n):
        d = fixture()
        try:
            src = 'fn(%s(path)) { print(path) }\n%s("example")\nif (false) { %s("never") }\n' % (n, n, n)
            rc, out, err = run(nift, d, src, vm, extra={"NIFT_VM": None} if vm else None)
            p = ok(not launched(d), "launched %s" % launched(d))
            return p + (ok(rc == 0 and out == "example\n" and not err, show(rc, out, err)) if vm else [])
        finally:
            shutil.rmtree(d, ignore_errors=True)
    CASES["user-function-default-" + _n] = _user_vm_default



def both_equal(src, files=None, extra=None, want=None, check=None):
    """The same rc, stdout and stderr in both engines (the VM's pass is compared with legacy's)."""
    def f(nift, vm):
        results = []
        for engine in (True, False):
            d = fixture(files)
            try:
                rc, out, err = run(nift, d, src, engine, extra=extra)
                results.append((rc, out, err, check(d) if check else True, launched(d)))
            finally:
                shutil.rmtree(d, ignore_errors=True)
        (rc, out, err, checked, l), legacy = results
        p = ok(results[0][:3] == legacy[:3], "VM %r != legacy %r" % (results[0][:3], legacy[:3]))
        p += ok(not l and not legacy[4], "launched")
        p += ok(checked and legacy[3], "fixture check failed")
        return p + (ok(want(rc, out, err), show(rc, out, err)) if want else [])
    return f


# D-096 3b: native::<name> reaches the Nift native even when a user function of that name exists (both engines,
# the same output; the user function never runs).
for _n in SEVEN:
    CASES["shadowed-qualified-" + _n] = both_equal(
        'fn(%s(x, y)) { print("user") }\n%s\n' % (_n, CALLS[_n][0]),
        want=lambda rc, out, err: rc == 0 and "user" not in out,
        check=lambda d, n=_n: CALLS[n][1](d, "A" if n == "cat" else "true\n" if n == "ls" else ""))


# A name outside the seven, every other spelling: reported before anything runs, same text and position.
for _name, _src, _message, _pos in (
        ("unknown-print", 'print("first")\nnative::print("x")\n', "native::print" + UNKNOWN, "main.f:2:1"),
        ("unknown-copy", 'print("first")\nx := native::copy("a.txt", "c.txt")\n', "native::copy" + UNKNOWN, "main.f:2:6"),
        ("unknown-foo", 'print("first")\nnative::foo()\n', "native::foo" + UNKNOWN, "main.f:2:1"),
        ("spelling-spaced", 'print("first")\nnative :: cp("a.txt", "c.txt")\n', MALFORMED, "main.f:2:1"),
        ("spelling-space-before-paren", 'print("first")\nnative::cp ("a.txt", "c.txt")\n', MALFORMED, "main.f:2:1"),
        ("spelling-space-after", 'print("first")\nnative::  cp("a.txt", "c.txt")\n', MALFORMED, "main.f:2:1"),
        ("spelling-no-call", 'print("first")\nf := native::cp\n', MALFORMED, "main.f:2:6"),
        ("spelling-member", 'print("first")\nx := "a"\nx.native::cp("a.txt")\n', MALFORMED, "main.f:3:3"),
        ("spelling-double", 'print("first")\nnative::native::cp("a.txt", "c.txt")\n', MALFORMED, "main.f:2:1"),
        ("spelling-value-argument", 'print("first")\nt := thread(native::touch, "w")\n', MALFORMED, "main.f:2:13")):
    CASES[_name] = both_equal(_src, want=lambda rc, out, err, m=_message, p=_pos: (
        rc == 1 and not out and ("error: %s: %s" % (p, m)) in err))

CASES["strings-and-comments"] = both_equal(
    'print("native::foo(")\n// native :: x\nprint(\'native :: y\')\n', want=lambda rc, out, err: (
        rc == 0 and out == "native::foo(\nnative :: y\n"))


@case("trailing-comment")
def _trailing(nift, vm):
    """A `native::` in a trailing comment is not read (legacy runs the statement; the VM does not structure a trailing
    comment, a pre-existing refusal)."""
    d = fixture()
    try:
        rc, out, err = run(nift, d, 'print("x") // native::z\n', vm)
        # legacy then reports its own (pre-existing) "expression cannot be empty" for any trailing comment
        return ok("native::" not in err and (out == "x\n" if not vm else "opaque" in err), show(rc, out, err))
    finally:
        shutil.rmtree(d, ignore_errors=True)
CASES["native-as-binding"] = both_equal('native := 3\nprint(native)\n', want=lambda rc, out, err: out == "3\n")
CASES["arguments-and-nesting"] = both_equal(
    'fn(f(x)) { print("f " + x); return x }\ns := f("a.txt")\nt := f("c.txt")\nnative::cp(s, t)\n'
    'native::cat(native::ls(".")[0])\n'
    'print(native::ls(".").length() > 2)\nx := 1 + native::ls(".").length()\nprint(x > 1)\n',
    want=lambda rc, out, err: rc == 0 and out.startswith("f a.txt\nf c.txt\n") and out.endswith("true\ntrue\n"),
    check=lambda d: read(d, "c.txt") == "A")
CASES["error-position"] = both_equal('x := 1\nnative::cat("missing.txt")\n', want=lambda rc, out, err: rc == 1)
CASES["prepared-loop"] = both_equal('for(i : [0, 1, 2]) { native::touch("f" + i) }\nprint(native::ls(".").length())\n',
                                    check=lambda d: all(read(d, "f%d" % k) == "" for k in range(3)))
CASES["deferred-name-certainty"] = both_equal('native::touch("t")\nif (false) { nope() }\nprint("ok")\n',
                                              want=lambda rc, out, err: rc == 0 and out == "ok\n")
CASES["read-limit"] = both_equal('native::cat("big.txt")\n', files={"big.txt": "x" * 4096},
                                 extra={"NIFT_READ_LIMIT": "1K"}, want=lambda rc, out, err: rc != 0 and "read limit" in err)
CASES["threads-and-async"] = both_equal(
    'fn(w()) { native::touch("w1"); return 1 }\nt := thread(w)\nprint(t.join())\n'
    'fn[async](a()) { native::cat("a.txt"); return 2 }\nprint(await a())\n',
    want=lambda rc, out, err: rc == 0 and out == "1\nA2\n", check=lambda d: read(d, "w1") == "")


@case("trace")
def _trace(nift, vm):
    """Host-call tracing: the qualified calls are traced as the natives they are (NIFT_TRACE_HOST, fd 3)."""
    d = fixture()
    try:
        tr = os.path.join(d, "trace.jsonl")
        with open(os.path.join(d, "main.f"), "w") as f:
            f.write('native::touch("tr")\nnative::cat("a.txt")\n')
        env = {"PATH": os.path.join(d, "fakebin") + ":/usr/bin:/bin", "HOME": d, "LANG": "C",
               "NIFT_VM": "require" if vm else "0", "NIFT_TRACE_HOST": "3"}
        p = subprocess.run(["/bin/sh", "-c", 'exec 3>"$1"; exec "$2" main.f', "sh", tr, nift], cwd=d, env=env,
                           capture_output=True, text=True, timeout=60, preexec_fn=limits)
        text = read(d, "trace.jsonl") or ""
        return ok(p.returncode == 0 and '"op":"touch"' in text and '"op":"read"' in text and "a.txt" in text,
                  "trace %r" % text[:300])
    finally:
        shutil.rmtree(d, ignore_errors=True)


@case("module-host-order")
def _module(nift, vm):
    """A qualified host op before an import keeps the R1 refusal (by native id, module-host-op:cp) on the VM."""
    d = fixture({"m2.nft": "x := 2\nexport(x)\n", "m.nft": "x := 1\nexport(x)\n"})
    try:
        rc, out, err = run(nift, d, 'native::cp("m2.nft", "m.nft")\nimport("m.nft")\nprint(x)\n', vm)
        if vm:
            return ok(rc == 1 and "module-host-op:cp" in err and read(d, "m.nft") == "x := 1\nexport(x)\n",
                      show(rc, out, err))
        return ok(rc == 0 and out == "2\n", show(rc, out, err))
    finally:
        shutil.rmtree(d, ignore_errors=True)


@case("session")
def _session(nift, vm):
    d = fixture()
    try:
        rc, out, err = run(nift, d, None, vm, args=["-i"], stdin='native::touch("s1")\nprint(native::ls(".").length() > 0)\n'
                           'touch("s2")\n')
        p = ok(read(d, "s1") == "" and "true" in out, show(rc, out, err)) + ok(not launched(d), "launched")
        if vm:
            return p + ok("NIFT_VM session refused: native-unqualified" in err and read(d, "s2") is None, show(rc, out, err))
        return p + ok(read(d, "s2") == "", show(rc, out, err))
    finally:
        shutil.rmtree(d, ignore_errors=True)


@case("site-template-and-hook")
def _site(nift, vm):
    """Templates and build hooks: native:: in an @script and a pre-build hook; the dependency is observed."""
    d = fixture()
    try:
        env = {"PATH": os.path.join(d, "fakebin") + ":/usr/bin:/bin", "HOME": d, "LANG": "C",
               "NIFT_VM": "require" if vm else "0"}
        s = os.path.join(d, "site")
        os.makedirs(s)
        subprocess.run([nift, "init"], cwd=s, env=env, capture_output=True, timeout=60, check=True)
        with open(os.path.join(s, "data.txt"), "w") as f:
            f.write("DATA")
        with open(os.path.join(s, "content", "index.pre-build.f"), "w") as f:
            f.write('native::touch("hook-marker")\n')
        with open(os.path.join(s, "content", "index.html"), "w") as f:
            f.write('L:@script { return native::ls("content").length() }\nR:@script { return native::cat("data.txt") }\n')
        ticks = [sys.executable, "-B", os.path.join(HERE, "cp01", "ticks.py")]  # no CP01 equal-tick rebuild (D-091 #3)
        subprocess.run(ticks + ["--age", s, "content/index.html", "content/index.pre-build.f", "data.txt", "content"],
                       cwd=s, capture_output=True, check=True)
        b = subprocess.run([nift, "build"], cwd=s, env=env, capture_output=True, text=True, timeout=120)
        page = read(s, "public/index.html") or ""
        p = ok(b.returncode == 0 and "L:2" in page and os.path.exists(os.path.join(s, "hook-marker")),
               "build rc %s %r page %r" % (b.returncode, b.stderr[-200:], page[:200]))
        st = subprocess.run([nift, "status"], cwd=s, env=env, capture_output=True, text=True, timeout=60)
        p += ok("up to date" in st.stdout, "status after build: %r" % st.stdout[-200:])
        with open(os.path.join(s, "data.txt"), "w") as f:  # a read through native::cat is a dependency
            f.write("DATA2!")
        subprocess.run(ticks + ["--age", s, "data.txt", "."], cwd=s, capture_output=True, check=True)
        st = subprocess.run([nift, "status"], cwd=s, env=env, capture_output=True, text=True, timeout=60)
        return p + ok("up to date" not in st.stdout, "the native::cat read is no dependency: %r" % st.stdout[-200:]) + \
            ok(not launched(d), "launched")
    finally:
        shutil.rmtree(d, ignore_errors=True)


def _tree(d):
    """Every file under d (fake PATH excluded) with its content: the filesystem effect of a run."""
    out = {}
    for dp, _, fs in os.walk(d):
        for f in fs:
            path = os.path.join(dp, f)
            if "fakebin" not in path and os.path.relpath(path, d) != "main.f":
                out[os.path.relpath(path, d)] = read(dp, f)
    return out


LEGACY_FORMS = 'print("s")\n%(p)stouch("t1")\n%(p)scat("a.txt")\nprint(%(p)sls(".").length())\n'


def f(nift, vm):
    """D-096 1: legacy execution (NIFT_VM=0) of native:: equals the unqualified legacy form: a script file and -e,
    the same rc, stdout, stderr and resulting files (a mutating native, touch, and cat)."""
    p = []
    for mode in ("file", "-e"):
        results = []
        for prefix in ("", "native::"):
            d = fixture()
            try:
                src = LEGACY_FORMS % {"p": prefix}
                args = ["main.f"] if mode == "file" else ["-e", src]
                rc, out, err = run(nift, d, src if mode == "file" else None, False, args=args)
                results.append((rc, out, err, _tree(d), launched(d)))
            finally:
                shutil.rmtree(d, ignore_errors=True)
        p += ok(results[0] == results[1] and results[0][0] == 0 and "t1" in results[0][3] and
                results[0][1].startswith("s\nA"), "%s: unqualified %r != qualified %r" % (mode, results[0][:4], results[1][:4]))
    return p


CASES["legacy-equivalence-script"] = f


def f(nift, vm):  # noqa: F811 (one registered function per case)
    """D-096 1: a site template (@script, legacy evaluator) with native::cat and native::touch builds the same page,
    output (cat writes to stdout) and files as the unqualified form."""
    results = []
    for prefix in ("", "native::"):
        d = fixture()
        try:
            env = {"PATH": os.path.join(d, "fakebin") + ":/usr/bin:/bin", "HOME": d, "LANG": "C"}
            s = os.path.join(d, "site")
            os.makedirs(s)
            subprocess.run([nift, "init"], cwd=s, env=env, capture_output=True, timeout=60, check=True)
            with open(os.path.join(s, "data.txt"), "w") as fh:
                fh.write("DATA")
            with open(os.path.join(s, "content", "index.html"), "w") as fh:
                fh.write('R:@script { %(p)stouch("tm"); return %(p)scat("data.txt") }\n' % {"p": prefix})
            b = subprocess.run([nift, "build"], cwd=s, env=env, capture_output=True, text=True, timeout=120)
            stdout = "".join(line for line in b.stdout.splitlines(True) if not line.startswith("time taken:"))
            results.append((b.returncode, stdout, b.stderr, read(s, "public/index.html"), read(s, "tm"),
                            launched(d)))
        finally:
            shutil.rmtree(d, ignore_errors=True)
    return ok(results[0] == results[1] and results[0][0] == 0 and results[0][1].startswith("DATA") and
              results[0][4] == "", "unqualified %r != qualified %r" % (results[0], results[1]))


CASES["legacy-equivalence-template"] = f


@case("unknown-unvalidated")
def _unvalidated(nift, vm):
    """NIFT_PREVALIDATE=off: the engines' own handling (template paths have no prevalidation): an error when reached,
    never the call; legacy's text and position."""
    d = fixture()
    try:
        rc, out, err = run(nift, d, 'print("first")\nnative::copy("a.txt", "c.txt")\n', vm,
                           extra={"NIFT_PREVALIDATE": "off"})
        return ok(rc == 1 and out == "first\n" and "main.f:2:1: native::copy" + UNKNOWN in err, show(rc, out, err)) + \
            ok(read(d, "c.txt") is None, "copy ran")
    finally:
        shutil.rmtree(d, ignore_errors=True)


def check(nift, only=""):
    if not (os.path.islink("/dev/stdout") and os.readlink("/dev/stdout") == "/proc/self/fd/1"):
        print("FAIL native-qualifier: /dev/stdout is not the link to /proc/self/fd/1")
        return ["environment"]
    failing = []
    for name, f in CASES.items():
        if only not in name:
            continue
        for vm in (True, False):
            if f.__name__ == "f" and not vm:
                continue  # both_equal compares the engines in one pass
            label = ("vm " if vm else "legacy ") + name if f.__name__ != "f" else "both " + name
            problems = f(nift, vm)
            print("%s native-qualifier %s%s" % ("FAIL" if problems else "PASS", label,
                                                (": " + "; ".join(problems)) if problems else ""))
            if problems:
                failing.append(label)
    total = sum(1 if f.__name__ == "f" else 2 for n, f in CASES.items() if only in n)
    print("native-qualifier: %d/%d pass" % (total - len(failing), total))
    return failing


def embed():
    """The embedding API (nift::Engine, legacy evaluator): native_qualifier_embed.cpp, built and run in scratch."""
    env = os.environ
    objs = [o for o in env.get("NIFT_TC_CLI_OBJECTS", "").split() if o not in ("src/nift.o", "src/CLI.o")]
    if not objs:
        print("FAIL native-qualifier embed: run through make (the harness toolchain environment)")
        return 1
    out = os.path.join(ROOT, ".build", "native-qualifier-embed")
    d = tempfile.mkdtemp(prefix="l6-embed-")
    try:
        # The embedding sources are not in the CLI's object set: compiled from source here, beside the test.
        embed_sources = ["src/embed/Engine.cpp", "src/embed/Context.cpp", "src/embed/program_api.cpp"]
        cmd = [env["NIFT_TC_CXX"]] + env.get("NIFT_TC_CPPFLAGS", "").split() + env.get("NIFT_TC_CXXFLAGS", "").split() + \
            [os.path.join(HERE, "native_qualifier_embed.cpp")] + embed_sources + objs + \
            ["-ldl", env["NIFT_TC_LIBFFI_A"], "-o", out]
        b = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        if b.returncode != 0:
            print("FAIL native-qualifier embed: build failed: %s" % b.stderr[-400:])
            return 1
        p = subprocess.run([out, d], capture_output=True, text=True, timeout=120)
        print(p.stdout.strip())
        return 0 if p.returncode == 0 else 1
    finally:
        shutil.rmtree(d, ignore_errors=True)
        if os.path.exists(out):
            os.remove(out)


# mutant: the cases that must fail
MUTANTS = {
    1: ["vm unknown-unvalidated"],  # the VM qualifier binds a name outside the seven
    2: ["vm unqualified-%s-%s" % (n, w) for n in SEVEN for w in ("top", "unreached", "function")],  # still bound
    3: ["both spelling-spaced", "both spelling-no-call", "both unknown-print"],  # no rule before running
    4: ["vm user-function-%s" % n for n in SEVEN],  # the seven stay reserved function names
    5: ["both arguments-and-nesting"],  # the receiver test reads `::` as an operator
    6: ["legacy unknown-unvalidated"],  # legacy binds a name outside the seven
}


def mutants(nift):
    # No-mutation control: the production binary passes every case, so a case failing on a mutant is the mutation's.
    control = check(nift)
    if control:
        print("FAIL native-qualifier mutants: no-mutation control: the production binary fails %s" % control)
        return 1
    print("PASS native-qualifier no-mutation control: the production binary passes every case")
    bad = 0
    for n, expected in sorted(MUTANTS.items()):
        out = os.path.join(ROOT, ".build", "mutant-nift", "native-qualifier-%d" % n)
        try:
            b = subprocess.run([sys.executable, "-B", os.path.join(HERE, "sanctioned_mutant.py"), "--out", out,
                                "--define=-DNIFT_L6_MUTANT=%d" % n], capture_output=True, text=True)
            if b.returncode != 0:
                print("FAIL native-qualifier mutant %d: harness: build failed: %s" % (n, b.stderr[-400:] or b.stdout[-400:]))
                bad += 1
                continue
            failing = check(os.path.join(out, "nift"))
            missed = [c for c in expected if c not in failing]
            if missed:
                print("FAIL native-qualifier mutant %d: not caught by %s" % (n, missed))
                bad += 1
            else:
                print("PASS native-qualifier mutant %d: caught by %s" % (n, ", ".join(expected)))
        finally:
            shutil.rmtree(out, ignore_errors=True)
    print("%s native-qualifier mutants: %d of %d caught" % ("FAIL" if bad else "PASS", len(MUTANTS) - bad, len(MUTANTS)))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift")
    ap.add_argument("--only", default="")
    ap.add_argument("--embed", action="store_true")
    ap.add_argument("--mutants", action="store_true")
    args = ap.parse_args()
    rc = 1 if args.nift and check(os.path.abspath(args.nift), args.only) else 0
    if args.embed:
        rc = max(rc, embed())
    if args.mutants and not args.nift:
        print("FAIL native-qualifier mutants: --mutants needs --nift (the no-mutation control)")
        return 1
    return max(rc, mutants(os.path.abspath(args.nift))) if args.mutants else rc


if __name__ == "__main__":
    sys.exit(main())
