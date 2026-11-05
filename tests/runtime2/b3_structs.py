#!/usr/bin/env python3
"""B3 struct type descriptors and privacy-aware struct rendering
(docs/runtime-2/checkpoints/b3-structs.md).

  b3_structs.py --nift ./nift            # the suite (make test-runtime2-b3)
  b3_structs.py --nift ./nift --update   # rewrite equiv/*.vm goldens

  parity/*.f    legacy (NIFT_VM=0) and the VM (NIFT_VM=require) give the same
                stdout, stderr and exit status, and the VM ran the program.
  disclose/*.f  presentation paths the VM refuses or reports as an error
                (interpolation, print, concatenation, validate, ...): no output
                of either engine contains a private field's value.
  equiv/*.f     thread and async transfers (legacy cannot transfer an
                instance): parallel scheduling gives NAME.vm; the cooperative
                scheduler refuses instances crossing a task (coro-struct, D7,
                unchanged by B3) before any effect.
  vm/*.f        VM-only behaviour where legacy differs (named in the fixture:
                an established difference, or the owner's ruling that an
                explicit call runs a struct's own presentation method): the
                VM's transcript is NAME.vm.
  refuse/*.f    refused before any effect with NAME.reason (exit 1, no stdout).
  modules/*/    main.f importing m.f: `reason` (refused before any effect) or
                `vm` (the VM's transcript; written by --update).
  structure     the serializer has no path to VM code: implicit presentation
                cannot run user code by construction (owner ruling).

No output of an implicit rendering may contain SIDE-EFFECT: a struct's own
stringify/prettify/highlight prints it, and only an explicit call may run it.
  SESSIONS      piped sessions: schema replacement keeps each instance's own
                descriptor; presentation in sessions; session display paths.

Every fixture keeps private values containing SENTINEL; no output may
contain it, on any path (the owner's non-disclosure requirement). Exit 0 when
every check passes; a mutant binary must make it fail (b3/mutants.sh).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "b3")
SENTINEL = "SECRET-b3"
SIDE = "SIDE-EFFECT"
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def run(nift, path, mode, work, extra=None, tree=None):
    d = tempfile.mkdtemp(dir=work)
    name = os.path.basename(path)
    if tree:  # a module case: the whole directory
        for f in os.listdir(tree):
            shutil.copy(os.path.join(tree, f), os.path.join(d, f))
    else:
        shutil.copy(path, os.path.join(d, name))
    stats = os.path.join(d, "stats.jsonl")
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "TMPDIR": d, "LANG": "C",
           "LC_ALL": "C", "TZ": "UTC", "NIFT_VM": mode, "NIFT_VM_STATS": stats}
    if extra:
        env.update(extra)
    p = subprocess.run(["timeout", "120", nift, name], cwd=d, env=env, stdin=subprocess.DEVNULL,
                       capture_output=True)
    st = None
    if os.path.exists(stats):
        try:
            st = json.loads(open(stats).read().splitlines()[-1])
        except (OSError, IndexError, ValueError):
            st = None
    norm = lambda b: b.decode("utf-8", "replace").replace(d + os.sep, "")
    return norm(p.stdout), norm(p.stderr), p.returncode, st


def run_session(nift, stdin, work, mode="require"):
    d = tempfile.mkdtemp(dir=work)
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "TMPDIR": d, "LANG": "C",
           "LC_ALL": "C", "TZ": "UTC", "NIFT_VM": mode, "NO_COLOR": "1"}
    p = subprocess.run(["timeout", "120", nift, "-i"], cwd=d, env=env, input=stdin.encode(), capture_output=True)
    return p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace"), p.returncode


SCHEMA = 'struct(P){a:=1;private b:="SECRET-b3";c:=3}'
PSM = "presentation-struct-method:stringify"
UMETHOD = 'struct(C){n:=0;private s:="SECRET-b3";fn(stringify()){print("SIDE-EFFECT");return "u"}}\n'
# (name, stdin, kind): "parity" = legacy-equal and no refusal; ("vm", out, reason) = the
# VM's stdout before the statement refused with `reason` (review N2: the reason is
# asserted); ("error", out, text) = an ordinary statement error (B4); plus
# non-disclosure on both engines.
SESSIONS = [
    # Schema replacement (D3): the old instance keeps its own descriptor, the
    # new one gets the new schema's (privacy swapped).
    ("schema-replacement", SCHEMA + ';p:=P()\nstruct(P){private a:="SECRET-b3";b:=2}\nq:=P()\n'
     'print(p.stringify())\nprint(q.stringify())\nprint([p, q].prettify())\nexit\n', "parity"),
    ("schema-repeated", 'struct(P){a:=1}\np:=P()\nstruct(P){private a:="SECRET-b3";b:=2}\nq:=P()\n'
     'struct(P){c:=3}\nr:=P()\nprint([p, q, r].stringify())\nexit\n', "parity"),
    # The same schema version across statements (one descriptor) and a later
    # statement presenting both.
    ("schema-reused", SCHEMA + '\np:=P()\nq:=P()\nprint([p, q].stringify())\nprint(q.prettify())\nexit\n', "parity"),
    # Formerly refused by Phase A (refsem_a.py SESSIONS session_struct_value / contents).
    ("struct-value", SCHEMA + '\nc := P()\nx := c\nprint("before")\nprint("x"); print(x.stringify())\nexit\n',
     "parity"),
    ("struct-contents", SCHEMA + '\nc := P()\na := [c]\nprint("before")\nprint("x"); print(a.stringify())\nexit\n',
     "parity"),
    ("session-expression", SCHEMA + '\nc := P()\nc.stringify()\nc.prettify()\nc.highlight()\nexit\n', "parity"),
    # B4 (b4-dispatch.md 6): the session display of a bare instance is its
    # descriptor form (legacy parity: public fields only, no user method runs).
    # Interpolating an instance from an earlier statement stays refused in a
    # session (the binding's kinds are not proven there); in a script it is an
    # ordinary error (b4/dispatch). Nothing private is shown either way.
    ("display-bare", SCHEMA + '\nc := P()\nc\nexit\n', "parity"),
    ("display-bare-user-method", UMETHOD + 'c := C()\nc\nexit\n', "parity"),
    ("display-interpolation", SCHEMA + '\nc := P()\nprint("v=$[c]")\nexit\n',
     ("vm", "", "interpolation:unknown-type")),
    ("display-interpolation-same-line", SCHEMA + '\nc := P(); print("v=$[c]")\nexit\n',
     ("error", "", "struct instances cannot be rendered as text")),
    # Owner ruling: implicit presentation in a session never runs the struct's
    # own stringify (no SIDE-EFFECT; the counter stays 0) ...
    ("implicit-methods", 'struct(C){n:=0;private s:="SECRET-b3";fn(stringify()){print("SIDE-EFFECT");n=n+1;'
     'return "u"}}\nc := C()\nxs := [c, c]\nprint(xs.stringify())\nprint({"k": c}.prettify())\n'
     'print(xs.stringify())\nexit\n', "parity"),
    # ... and an explicit call on a session value that may be one is refused.
    ("explicit-mixed", 'struct(C){n:=0;private s:="SECRET-b3";fn(stringify()){print("SIDE-EFFECT");return "u"}}\n'
     'xs := [C(), map()]\nfor (x : xs) { print(x.stringify()) }\nexit\n', ("vm", "", PSM)),
    # Review H1: a later statement that does not name the struct still sees an
    # earlier statement's user presentation method (D10 ignores those).
    ("h1-alias", UMETHOD + 'c := C()\nd := c\nprint(d.prettify())\nprint(d.stringify())\nexit\n',
     ("vm", 'C{\n  n: 0,\n}\n', PSM)),
    ("h1-index", UMETHOD + 'c := C()\nxs := [c]\nprint(xs[0].stringify())\nexit\n', ("vm", "", PSM)),
    # Review N2: an object lookup (the former map lookup was refused for an
    # unrelated reason, session:collection-result).
    ("h1-lookup", UMETHOD + 'c := C()\no := {"k": c}\nprint(o["k"].stringify())\nexit\n', ("vm", "", PSM)),
    # A session function: defined after the struct, or before it (the
    # function is recompiled with the session's later struct and refused).
    ("h1-function-after", UMETHOD + 'fn(show(x)) { return x.stringify() }\nc := C()\nprint(show(c))\nexit\n',
     ("vm", "", PSM)),
    ("h1-function-before", 'fn(show(x)) { return x.stringify() }\nprint(show([1]))\n' + UMETHOD +
     'c := C()\nprint(show(c))\nexit\n', ("vm", "[1]\n", PSM)),
    # Schema replacement removing the method: the old instance, proven, runs
    # its own method; an alias that may be it is refused.
    ("h1-replace-remove", UMETHOD + 'p := C()\nstruct(C){n:=1}\nq := C()\nprint(p.stringify())\n'
     'print(q.stringify())\nd := p\nprint(d.stringify())\nexit\n', ("vm", "SIDE-EFFECT\nu\nC{n:1}\n", PSM)),
    # Schema replacement adding the method: the proven old instance renders,
    # the new one runs its method, an alias is refused (it may be either).
    ("h1-replace-add", 'struct(C){n:=0}\np := C()\nd := p\nprint(d.stringify())\n' + UMETHOD +
     'q := C()\nprint(q.stringify())\nprint(p.stringify())\ne := p\nprint(e.stringify())\nexit\n',
     ("vm", "C{n:0}\nSIDE-EFFECT\nu\nC{n:0}\n", PSM)),
    # Review L3: a private presentation method is no user method from outside:
    # nothing refused, the built-in renders (legacy parity).
    ("l3-private", 'struct(C){n:=0;private s:="SECRET-b3";private fn(stringify()){print("SIDE-EFFECT");return "u"}}\n'
     'c := C()\nd := c\nprint(c.stringify())\nprint(d.stringify())\nexit\n', "parity"),
]


def transcript(out, err, rc):
    return out + "--- stderr\n" + err + "--- exit %d\n" % rc


def fixtures(kind):
    d = os.path.join(HERE, kind)
    return sorted(os.path.join(d, f) for f in os.listdir(d) if f.endswith(".f"))


def ran_on_vm(st):
    return st is not None and st.get("outcome") in ("vm", "vm-error") and st.get("legacy_executions", 1) == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--update", action="store_true")
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    failures = []
    count = 0

    def check(ok, what):
        nonlocal count
        count += 1
        if not ok:
            failures.append(what)
            print("FAIL " + what)

    def private(name, *texts):
        for t in texts:
            check(SENTINEL not in t, "%s: a private field's value was disclosed: %r" % (name, t[:400]))

    with tempfile.TemporaryDirectory(prefix="b3-") as work:
        for f in fixtures("parity"):
            name = "parity " + os.path.basename(f)
            lo, le, lr, _ = run(nift, f, "0", work)
            vo, ve, vr, st = run(nift, f, "require", work)
            check(ran_on_vm(st), "%s: not run by the VM: %s" % (name, ve.strip()[:300]))
            check((lo, le, lr) == (vo, ve, vr),
                  "%s: legacy %r/%r/%d vs VM %r/%r/%d" % (name, lo[-400:], le[-300:], lr, vo[-400:], ve[-300:], vr))
            private(name, lo, le, vo, ve)
            check(SIDE not in vo + lo, "%s: implicit presentation ran a struct's own method: %r" % (name, vo[-300:]))
        for f in fixtures("disclose"):
            name = "disclose " + os.path.basename(f)
            lo, le, lr, _ = run(nift, f, "0", work)
            vo, ve, vr, st = run(nift, f, "require", work)
            check(vr != 0, "%s: the VM accepted a path B3 leaves refused or erroring: %r" % (name, vo[-300:]))
            private(name, lo, le, vo, ve)
        for f in fixtures("refuse"):
            name = "refuse " + os.path.basename(f)
            reason = open(f[:-2] + ".reason").read().strip()
            vo, ve, vr, st = run(nift, f, "require", work)
            lo, le, lr, _ = run(nift, f, "0", work)
            check(vr == 1 and vo == "" and "(%s)" % reason in ve and "no part of it ran" in ve,
                  "%s: expected the refusal %s before effects, got %d %r %r" % (name, reason, vr, vo, ve[:300]))
            private(name, vo, ve, lo, le)
        mods = os.path.join(HERE, "modules")
        for case in sorted(os.listdir(mods)):
            tree = os.path.join(mods, case)
            name = "module " + case
            main = os.path.join(tree, "main.f")
            vo, ve, vr, st = run(nift, main, "require", work, tree=tree)
            lo, le, lr, _ = run(nift, main, "0", work, tree=tree)
            if os.path.exists(os.path.join(tree, "reason")):
                reason = open(os.path.join(tree, "reason")).read().strip()
                check(vr == 1 and vo == "" and "(%s)" % reason in ve and "no part of it ran" in ve,
                      "%s: expected %s before effects, got %d %r %r" % (name, reason, vr, vo, ve[:300]))
            else:
                got = transcript(vo, ve, vr)
                if args.update:
                    open(os.path.join(tree, "vm"), "w").write(got)
                    continue
                want = open(os.path.join(tree, "vm")).read() if os.path.exists(os.path.join(tree, "vm")) else None
                check(ran_on_vm(st) and got == want, "%s: output\n%s\nexpected\n%s" % (name, got, want))
            private(name, vo, ve, lo, le)
        for f in fixtures("equiv"):
            name = "equiv " + os.path.basename(f)
            base = f[:-2]
            po, pe, pr, pst = run(nift, f, "require", work)
            co, ce, cr, cst = run(nift, f, "require", work, extra={"NIFT_VM_COOPERATIVE_THREADS": "1"})
            lo, le, lr, _ = run(nift, f, "0", work)
            par = transcript(po, pe, pr)
            if args.update:
                open(base + ".vm", "w").write(par)
                continue
            check(ran_on_vm(pst), "%s: not run by the VM (parallel): %s" % (name, pe[:300]))
            want = open(base + ".vm").read() if os.path.exists(base + ".vm") else None
            check(par == want, "%s: output\n%s\nexpected\n%s" % (name, par, want))
            check(cr == 1 and co == "" and "(coro-struct)" in ce and "no part of it ran" in ce,
                  "%s: cooperative: expected the coro-struct refusal before effects, got %d %r %r" %
                  (name, cr, co, ce[:300]))
            private(name, po, pe, co, ce, lo, le)
            check(SIDE not in po, "%s: implicit presentation ran a struct's own method: %r" % (name, po[-300:]))
        for f in fixtures("vm"):
            name = "vm " + os.path.basename(f)
            base = f[:-2]
            vo, ve, vr, st = run(nift, f, "require", work)
            lo, le, lr, _ = run(nift, f, "0", work)
            got = transcript(vo, ve, vr)
            if args.update:
                open(base + ".vm", "w").write(got)
                continue
            want = open(base + ".vm").read() if os.path.exists(base + ".vm") else None
            check(ran_on_vm(st) and got == want, "%s: output\n%s\nexpected\n%s" % (name, got, want))
            private(name, vo, ve, lo, le)
        for name, stdin, kind in SESSIONS:
            name = "session " + name
            vo, ve, vr = run_session(nift, stdin, work)
            lo, le, lr = run_session(nift, stdin, work, mode="0")
            if kind == "parity":
                check((vo, ve, vr) == (lo, le, lr) and "refused" not in ve,
                      "%s: legacy %r/%r/%d vs VM %r/%r/%d" % (name, lo, le, lr, vo, ve, vr))
            elif kind[0] == "error":  # B4: an ordinary statement error (the session goes on)
                check(vo == kind[1] and kind[2] in ve and "refused" not in ve,
                      "%s: VM %r/%r/%d (expected stdout %r, then the error %s)" % (name, vo, ve[:300], vr, kind[1],
                                                                                 kind[2]))
            else:
                check(vo == kind[1] and vr == 1 and "session refused: %s" % kind[2] in ve,
                      "%s: VM %r/%r/%d (expected stdout %r, then the refusal %s)" % (name, vo, ve[:300], vr, kind[1],
                                                                                   kind[2]))
            private(name, vo, ve, lo, le)
            if kind == "parity":
                check(SIDE not in vo, "%s: implicit presentation ran a struct's own method: %r" % (name, vo))
        # Structure (owner ruling): the serializer cannot reach VM code. Its
        # entry point takes no Context, VM or callable; its file includes no VM,
        # program or compiler header; a descriptor carries no methods.
        fm = open(os.path.join(ROOT, "src/rt/natives/format_methods.cpp")).read()
        fh = open(os.path.join(ROOT, "src/rt/natives/format_methods.h")).read()
        st = open(os.path.join(ROOT, "src/rt/struct_type.h")).read()
        includes = [l.split('"')[1] for l in fm.splitlines() if l.startswith('#include "')]
        check(not any(i.startswith(("rt/vm", "rt/compiler", "rt/proto", "rt/session", "rt/codegen")) for i in includes),
              "structure: format_methods.cpp includes %s" % includes)
        check("bool serialize(const Value& value, bool pretty, std::string& out, std::string& error, bool& internal);"
              in fh, "structure: the serializer's signature changed (it must take no Context or VM)")
        check("Proto" not in st and "Function" not in st and "method" not in st.lower().replace("methods", ""),
              "structure: a struct descriptor must carry no methods or code")
    if args.update:
        print("updated goldens")
        return 0
    print("%s b3-structs: %d checks, %d failure(s)" % ("PASS" if not failures else "FAIL", count, len(failures)))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
