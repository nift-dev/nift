#!/usr/bin/env python3
"""C145 (D-102, D-106): F5 literal call arguments and F6 legacy method precedence, against legacy.

  literal_args.py --nift NIFT              every case
  literal_args.py --nift NIFT --mutants    the production binary passes every case (no-mutation control), then
                                           NIFT_C145_MUTANT=1..5, each built with sanctioned_mutant.py, must be caught

F5: legacy (CP10) passes a call argument that is exactly one quoted literal as its text; only the free output family
(print, err, warn) and output-stream writes interpolate it. The VM must give legacy's bytes (rc, stdout, stderr,
resulting files) for every callee kind and context, admit the cases only a fix can pass, and refuse
write/write_line/write_val with such a literal (host-literal-arg) before anything runs.
F6: a call of a plain object field named like one of the 46 pinned legacy method heads is refused
(legacy-method-precedence) before the field runs; every other name matches legacy.
Fixtures are fresh scratch directories (a.txt = "A", a file literally named `$[x]` = "LIT"); nothing outside them is
written, and /dev/stdout is never opened. Exit status: 0 = every case passes (and every mutant is caught).
"""
import argparse
import os
import re
import resource
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
PRE = 'x := "a.txt"\nfn(id(v)) { return v }\n'
ARGS = ['"$[x]"', "'$[x]'", '"pre-$[x]"', '"$[x]$[x]"', '"$[ x ]"', '"$[nope]"', '"$[1/0]"', '"$[x"', '"$[]"']


def limits():
    resource.setrlimit(resource.RLIMIT_AS, (2 << 30, 2 << 30))
    resource.setrlimit(resource.RLIMIT_FSIZE, (64 << 20, 64 << 20))


def fixture(files=None):
    d = tempfile.mkdtemp(prefix="c145-")
    for n, text in dict({"a.txt": "A", "$[x]": "LIT"}, **(files or {})).items():
        with open(os.path.join(d, n), "w") as f:
            f.write(text)
    return d


def tree(d):
    out = {}
    for dp, dirs, fs in os.walk(d):
        for n in dirs:
            out[os.path.relpath(os.path.join(dp, n), d) + "/"] = None
        for n in fs:
            if n == "main.f":
                continue
            with open(os.path.join(dp, n), "rb") as f:
                out[os.path.relpath(os.path.join(dp, n), d)] = f.read()
    return out


def run(nift, src, vm, files=None, args=None, stdin=None, extra=None):
    """(rc, stdout, stderr, files) in a fresh fixture; vm: True = VM default, False = NIFT_VM=0."""
    d = fixture(files)
    try:
        if src is not None:
            with open(os.path.join(d, "main.f"), "w") as f:
                f.write(src)
        env = {"PATH": "/usr/bin:/bin", "HOME": d, "LANG": "C", **(extra or {})}
        if not vm:
            env["NIFT_VM"] = "0"
        try:
            p = subprocess.run([nift] + (args or ["main.f"]), cwd=d, env=env, input=stdin, capture_output=True,
                               text=True, timeout=60, preexec_fn=limits)
        except subprocess.TimeoutExpired:
            return None, "", "timeout", {}
        return p.returncode, p.stdout, p.stderr.replace(d + "/", ""), tree(d)
    finally:
        shutil.rmtree(d, ignore_errors=True)


def show(r):
    return "rc %s out %r err %r" % (r[0], r[1][-160:], r[2][-240:])


CASES = {}


def equal_case(name, src, files=None, args=None, stdin=None, want=None, extra=None):
    """The VM default gives legacy's exact rc, stdout, stderr and files; `want(result)` must hold for the VM."""
    def f(nift):
        vm = run(nift, src, True, files, args, stdin, extra)
        legacy = run(nift, src, False, files, args, stdin, extra)
        problems = [] if vm == legacy else ["VM %s != legacy %s" % (show(vm), show(legacy))]
        if want and not want(vm):
            problems.append("VM result %s" % show(vm))
        return problems
    CASES[name] = f


# ---- F5 A1: callee kinds x argument spellings -------------------------------------------------------------------
CALLEES = {
    "user-fn": ("", "print(id(%s))"),
    "lambda": ("lam := (v) => v\n", "print(lam(%s))"),
    "param-fn": ("fn(app(f, v)) { return f(v) }\n", "print(app(id, %s))"),
    "field": ('o := {"f": (v) => v}\n', "print(o.f(%s))"),
    "thread": ("fn(w(v)) { return v }\n", "t := thread(w, %s)\nprint(t.join())"),
    "await": ("fn[async](af(v)) { return v }\n", "print(await af(%s))"),
    "error": ("", "e := error(%s)\nprint(e.message)"),
    "nested": ("", "print(id(id(%s)))"),
    "blanks": ("", "print(id( %s ))"),
    "second-arg": ("fn(two(a, b)) { return b }\n", 'print(two("k", %s))'),
}
for callee, (decl, call) in CALLEES.items():
    for k, arg in enumerate(ARGS):
        equal_case("f5-%s-%d" % (callee, k), PRE + decl + call % arg + "\n")
STRUCT = 'struct(P) {\n  v := ""\n  fn(P(a)) {\n    v = a\n  }\n  fn(m(a)) { return a }\n}\n'
for k, arg in enumerate(ARGS[:3]):
    equal_case("f5-struct-method-%d" % k, STRUCT + 'x := "a.txt"\np := P("k")\nprint(p.m(%s))\n' % arg)
    equal_case("f5-struct-ctor-%d" % k, STRUCT + 'x := "a.txt"\np := P(%s)\nprint(p.v)\n' % arg)

# ---- F5 A2: natives (file trees compared) -------------------------------------------------------------------------
NATIVES = {
    "exists": 'print(exists("$[x]"))\nprint(exists("$[x]y"))',
    "open": 'print(open("$[x]"))',
    "file": 'h := file("$[x]")\nh.open("r")\nprint(h.read_all())\nh.close()',
    "cat": 'native::cat("$[x]")',
    "cp": 'native::cp("$[x]", "c.txt")\nprint(open("c.txt"))',
    "mv": 'native::mv("$[x]", "m.txt")',
    "rm": 'native::rm("$[x]")',
    "touch": 'native::touch("$[y]")',
    "mkdir": 'native::mkdir("$[d]")',
    "ls": 'print(native::ls("$[x]").length())',
    "remove": 'remove("$[x]")',
    "move": 'move("$[x]", "m2.txt")',
    "starts_with": 'print("$$[x]z".length())\ns := "abc"\nprint(s.starts_with("$[x]"))',
    "split": 's := "a$[x]b"\nprint(s.split("$[x]").length())',
    "index_of": 's := "abc"\nprint(s.index_of("$[x]"))',
    "join": 'print(["p", "q"].join("$[x]"))',
    "push": 'a := []\na.push("$[x]")\nprint(a)\nprint(a.contains("$[x]"))',
    "set-add": 'st := set()\nst.add("$[x]")\nprint(st.size())',
    "object-has": 'o := {"k": 1}\nprint(o.has("$[x]"))\nprint(o.get("$[x]"))',
}
for name, body in NATIVES.items():
    equal_case("f5-native-%s" % name, PRE + body + "\n")

# ---- F5 A3: interpolating controls --------------------------------------------------------------------------------
CONTROLS = {
    "print": ('print("$[x]")', "a.txt\n"),
    "print-single": ("print('$[x]')", "a.txt\n"),
    "print-concat": ('print("a" + "$[x]")', "aa.txt\n"),
    "arg-expression": ('print(id("$[x]" + ""))', "a.txt\n"),
    "arg-parenthesised": ('print(id(("$[x]")))', "a.txt\n"),
    "arg-receiver": ('print(id("$[x]".length()))', "5\n"),
    "assignment": ('s := "$[x]"\nprint(s)', "a.txt\n"),
    "return": ('fn(r()) { return "$[x]" }\nprint(r())', "a.txt\n"),
    "print-of-call": ('print(id("$[x]"))', "$[x]\n"),
    "no-marker": ('print(id("plain"))', "plain\n"),
    "escape": ('print(id("a\\tb").length())', "3\n"),
}
for name, (body, out) in CONTROLS.items():
    equal_case("f5-control-%s" % name, PRE + body + "\n", want=lambda r, o=out: r[0] == 0 and r[1] == o)

# ---- F5 A4: contexts ------------------------------------------------------------------------------------------------
CONTEXTS = {
    "for": 'for(i : [1, 2]) { print(id("$[x]")) }',
    "while": 'i := 0\nwhile (i < 2) { print(id("$[x]")); i += 1 }',
    "function": 'fn(g()) { return id("$[x]") }\nprint(g())',
    "lambda": 'h := () => id("$[x]")\nprint(h())',
    "async": 'fn[async](g()) { return id("$[x]") }\nprint(await g())',
    "thread": 'fn(g()) { return id("$[x]") }\nt := thread(g)\nprint(t.join())',
    "loop-native": 'for(i : [1]) { print(exists("$[x]")) }',
}
for name, body in CONTEXTS.items():
    equal_case("f5-context-%s" % name, PRE + body + "\n", want=lambda r: r[0] == 0 and ("$[x]" in r[1] or
                                                                                         r[1] == "true\n"))
equal_case("f5-context-module", 'import("m.f")\nprint(g())\n',
           files={"m.f": 'fn(id(v)) { return v }\nfn(g()) { return id("$[x]") }\nexport(g)\n'},
           want=lambda r: r[0] == 0 and r[1] == "$[x]\n")
equal_case("f5-context-e", None, args=["-e", 'fn(id(v)) { return v }\nprint(id("$[1+1]"))'],
           want=lambda r: r[0] == 0 and r[1] == "$[1+1]\n")
equal_case("f5-context-session", None, args=["-i"],
           stdin='fn(id(v)) { return v }\nprint(id("$[1+1]"))\nnative::cat("a$[1-1]")\n',
           files={"a$[1-1]": "Z"}, want=lambda r: "$[1+1]" in r[1] and "Z" in r[1])


def _trace(nift):
    """Host-call tracing (NIFT_TRACE_HOST=3 on fd 3, a scratch file): the raw name is what the native reads."""
    problems, traces = [], []
    for vm in (True, False):
        d = fixture()
        try:
            with open(os.path.join(d, "main.f"), "w") as f:
                f.write(PRE + 'native::cat("$[x]")\nprint(exists("$[x]"))\n')
            env = {"PATH": "/usr/bin:/bin", "HOME": d, "LANG": "C", "NIFT_TRACE_HOST": "3"}
            if not vm:
                env["NIFT_VM"] = "0"
            tr = os.path.join(d, "trace.jsonl")
            p = subprocess.run(["/bin/sh", "-c", 'exec 3>"$1"; exec "$2" main.f', "sh", tr, nift], cwd=d, env=env,
                               capture_output=True, text=True, timeout=60, preexec_fn=limits)
            text = open(tr).read() if os.path.exists(tr) else ""
            traces.append(text.replace(d, "<D>"))
            if p.returncode != 0 or p.stdout != "LITtrue\n" or "/$[x]" not in text or "a.txt" in text:
                problems.append("%s: rc %s out %r trace %r" % ("vm" if vm else "legacy", p.returncode, p.stdout,
                                                               text[:300]))
        finally:
            shutil.rmtree(d, ignore_errors=True)
    return problems + ([] if len(set(traces)) == 1 else ["traces differ: %r" % traces])


CASES["f5-trace"] = _trace

# ---- F5 C: cases only a fix (not a refusal) can pass, on the VM default --------------------------------------------
FIX_ONLY = {
    "id": ('print(id("$[x]"))', lambda r: r[0] == 0 and r[1] == "$[x]\n"),
    "exists-branch": ('if (exists("$[x]x")) { print("yes") } else { print("no") }', lambda r: r[1] == "no\n"),
    "rm": ('native::rm("$[x]")', lambda r: r[0] == 0 and "$[x]" not in r[3] and r[3].get("a.txt") == b"A"),
    "nope": ('print(id("$[nope]"))', lambda r: r[0] == 0 and r[1] == "$[nope]\n"),
}
for name, (body, want) in FIX_ONLY.items():
    def _fix(nift, src=PRE + body + "\n", want=want):
        r = run(nift, src, True)
        return [] if want(r) else ["VM default %s" % show(r)]
    CASES["f5-fix-" + name] = _fix


# ---- F5 A5/A6: the write-family fallback and unchanged refusals --------------------------------------------------
def refused(reason, src, files=None):
    def f(nift):
        vm = run(nift, src, True, files)
        legacy = run(nift, src, False, files)
        problems = [] if vm[0] == 1 and not vm[1] and ("(%s" % reason) in vm[2] else ["VM %s" % show(vm)]
        return problems + ([] if legacy[0] == 0 and legacy[1].startswith("before") else ["legacy %s" % show(legacy)])
    return f


CASES["f5-fallback-struct-write"] = refused(
    "host-literal-arg", 'struct(S) {\n  fn(write(v)) { return v }\n}\nprint("before")\ns := S()\n'
                        'x := "a.txt"\nprint(s.write("$[x]"))\n')
CASES["f5-fallback-ofstream-write"] = refused(
    "host-literal-arg", 'print("before")\nx := "a"\no := ofstream("out.txt")\no.write("$[x]")\nclose(o)\n')
CASES["f5-unchanged-backslash"] = refused("opaque", PRE + 'print("before")\nprint(id("\\$[x]"))\n')
CASES["f5-unchanged-array-literal"] = refused("opaque", PRE + 'print("before")\na := ["$[x]"]\nprint(a.length())\n')

# ---- F6: legacy method precedence -----------------------------------------------------------------------------------
PINNED = ("all any append cat count count_by cwd done elapsed env filter find find_index group_by group_by_each "
          "index_by insert insert_after insert_before map max_by min_by partition pause paused pipe prepend "
          "read_all_bytes read_bytes read_val reduce replace_once reset resume run running sort_by start status "
          "stderr stdin stdout stop unique_by write_bytes write_val").split()


# Legacy calls the field for these; the VM refuses them as builtin-receiver:* before L6 and C145 (pre-existing,
# outside F6, review-f5 DESIGN-C145 6b). Refused before the field runs, never as legacy-method-precedence.
PRE_EXISTING = {"front", "top"}


def kmethods():
    text = open(os.path.join(ROOT, "src", "rt", "legacy_names.cpp")).read()
    body = text[text.index("kMethods[] = {"):]
    return re.findall(r'"([^"]+)"', body[:body.index("};")])


def f6_program(name, arity):
    params = ", ".join("p%d" % k for k in range(arity))
    args = ", ".join('"a%d"' % k for k in range(arity))
    return ('print("before")\no := {"%s": (%s) => { print("RAN"); return "U" }}\nprint(o.%s(%s))\n'
            % (name, params, name, args))


def _f6(nift):
    problems = []
    names = kmethods()
    if len(names) != 136 or not set(PINNED) <= set(names):
        return ["the legacy method table changed: %d names" % len(names)]
    for name in names:
        for arity in (0, 1, 2):
            src = f6_program(name, arity)
            vm, legacy = run(nift, src, True), run(nift, src, False)
            label = "%s/%d" % (name, arity)
            if legacy[0] == 0 and name in PRE_EXISTING:
                if vm[0] != 1 or vm[1] or "(builtin-receiver:" not in vm[2]:
                    problems.append("%s: pre-existing refusal changed: %s" % (label, show(vm)))
            elif legacy[0] == 0:
                if vm[:2] != legacy[:2]:
                    problems.append("%s: legacy runs (%r) but the VM gives %s" % (label, legacy[1], show(vm)))
            elif vm[0] == 0 or "RAN" in vm[1] or "U\n" in vm[1]:
                problems.append("%s: legacy errors but the VM ran the field: %s" % (label, show(vm)))
            refused_f6 = "(legacy-method-precedence)" in vm[2]
            if refused_f6 and (name not in PINNED or vm[1]):
                problems.append("%s: refused as legacy-method-precedence: %s" % (label, show(vm)))
            if name in PINNED and arity == 1 and not refused_f6:
                problems.append("%s: pinned name not refused: %s" % (label, show(vm)))
    return problems


CASES["f6-oracle"] = _f6
for name in ("f", "push", "cp"):
    equal_case("f6-control-field-%s" % name, 'o := {"%s": (v) => "U"}\nprint(o.%s("a"))\n' % (name, name),
               want=lambda r: r[0] == 0 and r[1] == "U\n")
equal_case("f6-control-struct-cat", 'struct(S) {\n  fn(cat(p)) { return "S" }\n}\ns := S()\nprint(s.cat("a"))\n',
           want=lambda r: r[0] == 0 and r[1] == "S\n")
equal_case("f6-control-native-cat", 'native::cat("a.txt")\n', want=lambda r: r[0] == 0 and r[1] == "A")


def _f6_entry(nift):
    problems = []
    e = run(nift, None, True, args=["-e", 'print("before")\no := {"cat": (p) => "U"}\nprint(o.cat("a"))'])
    if e[0] != 1 or e[1] or "(legacy-method-precedence)" not in e[2]:
        problems.append("-e: %s" % show(e))
    m = run(nift, 'print("before")\nimport("m.f")\nprint(g())\n', True,
            files={"m.f": 'fn(g()) {\n  o := {"cat": (p) => "U"}\n  return o.cat("a")\n}\nexport(g)\n'})
    if m[0] != 1 or m[1] or "(legacy-method-precedence)" not in m[2]:
        problems.append("module: %s" % show(m))
    # Sessions keep their own refusal, session:legacy-method-precedence (D-106: not narrowed or replaced by C145).
    s = run(nift, None, True, args=["-i"], stdin='o := {"cat": (p) => "U"}\nprint(o.cat("a"))\n')
    if "session refused: session:legacy-method-precedence" not in s[2] or "U" in s[1]:
        problems.append("session: %s" % show(s))
    return problems


CASES["f6-entry-points"] = _f6_entry


def check(nift, only="", quiet=False):
    failing = []
    for name, f in CASES.items():
        if only not in name:
            continue
        problems = f(nift)
        if problems:
            failing.append(name)
        if problems or not quiet:
            print("%s literal-args %s%s" % ("FAIL" if problems else "PASS", name,
                                            (": " + "; ".join(problems)[:600]) if problems else ""))
    total = sum(1 for n in CASES if only in n)
    print("literal-args: %d/%d pass" % (total - len(failing), total))
    return failing


MUTANTS = {
    1: ["f5-fix-id", "f5-fix-rm", "f5-user-fn-0"],  # the raw literal interpolates again
    2: ["f5-control-print"],                         # the output family takes raw literals too
    3: ["f5-fallback-struct-write"],                 # the write-family fallback dropped
    4: ["f6-oracle", "f6-entry-points"],             # the F6 guard removed
    5: ["f6-oracle", "f6-control-field-push"],       # an over-refused name (push) added to the pinned list
}


def mutants(nift):
    control = check(nift, quiet=True)
    if control:
        print("FAIL literal-args mutants: no-mutation control: the production binary fails %s" % control)
        return 1
    print("PASS literal-args no-mutation control: the production binary passes every case")
    bad = 0
    for n, expected in sorted(MUTANTS.items()):
        out = os.path.join(ROOT, ".build", "mutant-nift", "literal-args-%d" % n)
        try:
            b = subprocess.run([sys.executable, "-B", os.path.join(HERE, "sanctioned_mutant.py"), "--out", out,
                                "--define=-DNIFT_C145_MUTANT=%d" % n], capture_output=True, text=True)
            if b.returncode != 0:
                print("FAIL literal-args mutant %d: harness: build failed: %s" % (n, (b.stderr or b.stdout)[-400:]))
                bad += 1
                continue
            failing = check(os.path.join(out, "nift"), quiet=True)
            missed = [c for c in expected if c not in failing]
            if missed:
                print("FAIL literal-args mutant %d: not caught by %s" % (n, missed))
                bad += 1
            else:
                print("PASS literal-args mutant %d: caught by %s" % (n, ", ".join(expected)))
        finally:
            shutil.rmtree(out, ignore_errors=True)
    print("%s literal-args mutants: %d of %d caught" % ("FAIL" if bad else "PASS", len(MUTANTS) - bad, len(MUTANTS)))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--only", default="")
    ap.add_argument("--mutants", action="store_true")
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    if args.mutants:
        return mutants(nift)
    return 1 if check(nift, args.only) else 0


if __name__ == "__main__":
    sys.exit(main())
