#!/usr/bin/env python3
"""B0 pre-release safety: an admitted program never stops with an internal VM
failure because of a run-time operand kind (docs/runtime-2/checkpoints/b0-safety.md).

  safety.py --nift ./nift            # the suite (make test-runtime2-safety)
  safety.py --nift ./nift --update   # rewrite expected.json from this binary
  safety.py --nift ./nift --list     # the generated case names

Every program prints first. Each case is one of:
  error    an ordinary program error after the earlier output: exit 1 (a
           piped session: the statement fails, the session continues, exit 0),
           the earlier output present, one positioned `error: FILE:L:C: ...`
           line (sessions report no position, as legacy's sessions);
  refuse   refused before any effect (the compiler can see it): exit 1, no
           stdout, "no part of it ran", a real position;
  ok       runs to completion.
For every case, whatever its outcome: no exit 70, no "internal failure" and no
"compiler defect" text; and stdout, stderr and the exit status equal the
reviewed transcript in expected.json. Cases marked `legacy` must also equal
NIFT_VM=0 byte for byte (the same program, the same transcript).

Exit 0 when every check passes. Each mutant of safety/mutants.sh must fail it.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
EXPECTED = os.path.join(HERE, "expected.json")
INTERNAL = ("internal failure", "compiler defect", "unexpected operands")

# ---------------------------------------------------------------- handles (a)
HANDLES = {"mutex": "mutex()", "channel": "channel()", "atomic_int": "atomic<int>(1)",
           "atomic_bool": "atomic<bool>(true)"}
# container -> setup lines building `v` from the handle expression H (depth 1 and 2)
CONTAINERS = {
    "alone": ["v := H"],
    "array": ["v := [H]"],
    "array2": ["v := [1, [H]]"],
    "object": ['v := {"k": H}'],
    "object2": ['v := {"k": {"j": H}}'],
    "map": ["v := map()", 'v.set("k", H)'],
    "map2": ["v := map()", 'v.set("k", [H])'],
    "stack": ["v := stack()", "v.push(H)"],
    "stack2": ["v := stack()", "v.push([H])"],
}
PRESENT = ("stringify", "prettify", "highlight")
FORMS = ("top", "fn", "closure", "module", "session")


def handle_case(form, setup, expr):
    """(files, argv, stdin): a program presenting `expr` built by `setup`."""
    body = "\n".join("  " + s for s in setup)
    if form == "top":
        return {"p.f": 'print("b")\n' + "\n".join(setup) + "\nprint(" + expr + ')\nprint("unreached")\n'}, None
    if form == "fn":
        return {"p.f": "fn(show()) {\n" + body + "\n  return " + expr + '\n}\nprint("b")\nprint(show())\n'}, None
    if form == "closure":
        return {"p.f": "f := () => {\n" + body + "\n  return " + expr + '\n}\nprint("b")\nprint(f())\n'}, None
    if form == "module":
        return {"m.f": "fn(show()) {\n" + body + "\n  return " + expr + "\n}\nexport(show)\n",
                "p.f": 'print("b")\nimport("m.f")\nprint(show())\n'}, None
    return None, 'print("b")\n' + "\n".join(setup) + "\nprint(" + expr + ')\nprint("after")\n'


def cases():
    out = []  # (name, files|None, session stdin|None, argv, legacy parity)

    def add(name, files=None, stdin=None, argv=(), legacy=False):
        out.append((name, files, stdin, tuple(argv), legacy))

    for h, ctor in HANDLES.items():
        for c, setup in CONTAINERS.items():
            lines = [s.replace("H", ctor) for s in setup]
            for m in PRESENT:
                for form in FORMS:
                    files, stdin = handle_case(form, lines, "v." + m + "()")
                    add("h_%s_%s_%s_%s" % (h, c, m, form), files, stdin)
            files, stdin = handle_case("top", lines + ["w := [v]"], "validate({}, w)")
            add("h_%s_%s_validate" % (h, c), files)
    for h, ctor in (("mutex", "mutex()"), ("channel", "channel()")):
        for form in FORMS:
            # print paths and interpolation
            files, stdin = handle_case(form, ["v := " + ctor], "v")
            add("h_%s_print_%s" % (h, form), files, stdin)
            files, stdin = handle_case(form, ["v := [" + ctor + "]"], "v")
            add("h_%s_print_array_%s" % (h, form), files, stdin)
            files, stdin = handle_case(form, ["v := " + ctor], '"x$[v]"')
            add("h_%s_interp_%s" % (h, form), files, stdin)
            files, stdin = handle_case(form, ["v := [" + ctor + "]"], '"x$[v.stringify()]"')
            add("h_%s_interp_stringify_%s" % (h, form), files, stdin)
        # json_stringify is not a script built-in on either engine. Since slot 3 (C134) the call is reached and raises
        # `undefined callable: json_stringify`. For channel() legacy cannot be the oracle: legacy has no script
        # channel() (it raises `undefined callable: channel` at 2:7), and the VM's channel() is an approved VM-only
        # built-in (D-073 #6, owner 2026-11-11 22:23). That case has the explicit VM oracle VM_ORACLES below.
        add("h_%s_json_stringify" % h, {"p.f": 'print("b")\nv := [%s]\nprint(json_stringify(v))\n' % ctor})
        # a handle reached through an imprecise path: a parameter, a map get, a loop element
        add("h_%s_param" % h, {"p.f": 'fn(show(x)) { return [x].stringify() }\nprint("b")\nprint(show(1))\n'
                                     'print(show(%s))\n' % ctor})
        add("h_%s_mapget" % h, {"p.f": 'm := map()\nm.set("k", %s)\nx := m.get("k")\nprint("b")\n'
                                      'print([x].stringify())\n' % ctor})
        add("h_%s_loop" % h, {"p.f": 'print("b")\nxs := [%s]\nfor (x : xs) { print([x].stringify()) }\n' % ctor})
        # session display of a statement value: a bare statement, a statement tail
        add("h_%s_session_bare" % h, stdin='print("b")\nv := %s\nv\nprint("after")\n' % ctor)
        add("h_%s_session_tail" % h, stdin='print("b")\nv := %s; v\nprint("after")\n' % ctor)
        add("h_%s_session_tail_call" % h, stdin='print("b")\nx := 1; %s\nprint("after")\n' % ctor)
        # catchability: the presentation error is an ordinary (fatal) error, as for a function value
        add("h_%s_try" % h, {"p.f": 'print("b")\nv := [%s]\ntry { print(v.stringify()) } catch (e) '
                                   '{ print("caught " + e.message) }\nprint("after")\n' % ctor})
    add("h_atomic_session_tail", stdin='print("b")\na := atomic<int>(1); a\nprint("after")\n')
    add("h_map_session_bare_earlier", stdin='s := map()\nprint("b")\ns\nprint("after")\n')
    add("h_set_session_tail_earlier", stdin='s := set()\nprint("b")\nx := 1; s\nprint("after")\n')
    add("h_validate_struct", {"p.f": 'struct(P) {\n  v := 0\n}\nprint("b")\nv := [P()]\nprint(validate({}, v))\n'})
    add("h_validate_map", {"p.f": 'print("b")\nm := map()\nv := [m]\nprint(validate({}, v))\n'})
    add("h_validate_function", {"p.f": 'print("b")\nf := () => 1\nv := [f]\nprint(validate({}, v))\n'})
    # the other handle kinds: host handles and tasks/futures are refused before any effect
    add("h_file", {"p.f": 'print("b")\nf := file("x.txt")\nprint([f].stringify())\n'})
    add("h_ofstream", {"p.f": 'print("b")\nf := ofstream("y.txt")\nprint([f].stringify())\n'})
    add("h_future", {"p.f": 'fn[async](g()) { return 1 }\nprint("b")\nx := g()\nprint([x].stringify())\n'})
    add("h_future_pushed", {"p.f": 'fn[async](g()) { return 1 }\nprint("b")\nxs := []\nxs.push(g())\n'
                                   'print(xs.stringify())\n'})
    add("h_thread", {"p.f": 'print("b")\nt := thread(() => 1)\nprint([t].stringify())\n'})
    # controls: a function value (the existing rule, legacy-equal) and a catchable error (try works)
    add("h_function_control", {"p.f": 'print("b")\nf := () => 1\nprint([f].stringify())\n'}, legacy=True)
    add("h_function_try_control", {"p.f": 'print("b")\nf := () => 1\ntry { print([f].stringify()) } catch (e) '
                                          '{ print("caught " + e.message) }\nprint("after")\n'})
    add("catch_control", {"p.f": 'print("b")\nm := map()\ntry { m.set("k", m) } catch (e) '
                                 '{ print("caught " + e.message) }\nprint("after")\n'})

    # interpolation of a value whose kinds mix a scalar with a non-scalar (review F1):
    # the lowering binds to_string (or a string append) from the scalar part
    struct = 'struct(P) {\n  v := 0\n}\n'
    kinds = {"mutex": "mutex()", "channel": "channel()", "atomic_int": "atomic<int>(1)",
             "atomic_bool": "atomic<bool>(true)", "map": "map()", "set": "set()", "stack": "stack()",
             "function": "(() => 1)", "struct": "P()"}
    for k, ctor in kinds.items():
        decl = struct if k == "struct" else ""
        for first, tag in (("1", "int"), ('"s"', "str")):
            add("i_%s_loop_%s" % (k, tag), {"p.f": decl + 'print("b")\nxs := [%s, %s]\nfor (v : xs) { print("i$[v]") }\n'
                                                  % (first, ctor)})
            add("i_%s_param_%s" % (k, tag), {"p.f": decl + 'fn(show(v)) { return "i$[v]" }\nprint("b")\n'
                                                   'print(show(%s))\nprint(show(%s))\n' % (first, ctor)})
            add("i_%s_closure_%s" % (k, tag), {"p.f": decl + 'g := (v) => "i$[v]"\nprint("b")\nprint(g(%s))\n'
                                                     'print(g(%s))\n' % (first, ctor)})
            add("i_%s_pop_%s" % (k, tag), {"p.f": decl + 'aa := [%s, %s]\nprint("b")\nv := aa.pop()\n'
                                                 'print("i$[v]")\n' % (first, ctor)})
            add("i_%s_mapget_%s" % (k, tag), {"p.f": decl + 'mm := map()\nmm.set("a", %s)\nmm.set("b", %s)\n'
                                                    'print("b")\nfor (key : ["a", "b"]) { v := mm.get(key); '
                                                    'print("i$[v]") }\n' % (first, ctor)})
        add("i_%s_session" % k, stdin=decl + 'print("b")\nfor (v : [1, %s]) { print("i$[v]") }\nprint("after")\n'
            % ctor)
    for tag, first in (("int", "1"), ("str", '"s"')):
        add("i_struct_module_%s" % tag, {"m.f": struct + "fn(mk()) {\n  return P()\n}\nexport(mk)\n",
                                        "p.f": 'import("m.f")\nfn(show(v)) { return "i$[v]" }\nprint("b")\n'
                                               'print(show(%s))\nprint(show(mk()))\n' % first})

    # ------------------------------------------------------------ mutators (b)
    for n in (0, 1, 2):
        add("m_args_push_%d" % n, {"p.f": 'print("before")\nfor (a : args) { a.push(1) }\nprint("after")\n'},
            argv=["x", "y"][:n], legacy=True)
    add("m_args_push_fn", {"p.f": 'fn(f()) { for (a : args) { a.push(1) }; return 0 }\nprint("before")\nf()\n'},
        argv=["x"], legacy=True)
    add("m_args_push_closure", {"p.f": 'g := () => { for (a : args) { a.push(1) }; return 0 }\nprint("before")\n'
                                       'g()\n'}, argv=["x"], legacy=True)
    add("m_args_push_nested", {"p.f": 'print("before")\nfor (a : args) { for (c : args) { c.push(1) } }\n'},
        argv=["x"], legacy=True)
    add("m_args_push_try", {"p.f": 'print("before")\nfor (a : args) { try { a.push(1) } catch (e) '
                                   '{ print("caught " + e.message) } }\nprint("after")\n'}, argv=["x"], legacy=True)
    add("m_args_push_module", {"m.f": 'fn(grow(xs)) {\n  for (a : xs) { a.push(1) }\n  return 0\n}\nexport(grow)\n',
                               "p.f": 'print("before")\nimport("m.f")\ngrow(["s"])\n'}, legacy=True)
    add("m_args_push_session", stdin='print("before")\nfor (a : args) { a.push(1) }\nprint("after")\n',
        argv=["x"])
    array_mutators = {"push": "push(1)", "pop": "pop()", "insert": "insert(0, 1)", "remove": "remove(0)",
                      "splice": "splice(0, 1)", "splice3": "splice(0, 1, [2])", "sort": "sort()",
                      "sortc": "sort((x, y) => x < y)", "reverse": "reverse()", "clear": "clear()"}
    receivers = {"string": '"s"', "number": "5", "object": '{"k": 1}', "null": "null"}
    for m, call in array_mutators.items():
        for r, lit in receivers.items():
            add("m_%s_%s_loop" % (m, r), {"p.f": 'print("b")\nxs := [%s]\nfor (a : xs) { a.%s }\n'
                                                 'print("after")\n' % (lit, call)}, legacy=m == "push")
            add("m_%s_%s_param" % (m, r), {"p.f": 'fn(f(a)) { a.%s; return 0 }\nprint("b")\nf([1])\nf(%s)\n'
                                                  % (call, lit)})
            add("m_%s_%s_mapget" % (m, r), {"p.f": 'print("b")\nm := map()\nm.set("k", %s)\na := m.get("k")\n'
                                                   'a.%s\n' % (lit, call)})
            add("m_%s_%s_session" % (m, r), stdin='print("b")\nfor (a : [%s]) { a.%s }\nprint("after")\n'
                % (lit, call))
        for src, setup in (("set", 's := set()\ns.add("q")\n'), ("stack", 's := stack()\ns.push("q")\n')):
            add("m_%s_%sloop" % (m, src), {"p.f": 'print("b")\n%sfor (a : s) { a.%s }\n' % (setup, call)})
        add("m_%s_rangeloop" % m, {"p.f": 'print("b")\nfor (a : range(2)) { a.%s }\n' % call},
            legacy=m == "push")
    keyed = {"map_set": 'set("k", 1)', "map_remove": 'remove("k")', "set_add": "add(1)", "stack_pop": "pop()",
             "clear": "clear()", "map_get": 'get("k")', "set_contains": "contains(1)", "stack_top": "top()"}
    wrong = dict(receivers, array="[1]", map="map()", set="set()", stack="stack()")
    for m, call in keyed.items():
        for r, lit in wrong.items():
            add("k_%s_%s_loop" % (m, r), {"p.f": 'print("b")\nxs := [%s]\nfor (a : xs) { a.%s }\n' % (lit, call)})
            add("k_%s_%s_param" % (m, r), {"p.f": 'fn(f(a)) { a.%s; return 0 }\nprint("b")\nf(map())\nf(%s)\n'
                                                  % (call, lit)})
            add("k_%s_%s_mapget" % (m, r), {"p.f": 'print("b")\nm := map()\nm.set("j", stack())\n'
                                                   'm.set("k", %s)\na := m.get("k")\na.%s\n' % (lit, call)})
    # a stack element mutated as a stack (`push`) reaches the array mutation path
    for r, lit in wrong.items():
        add("k_stack_push_%s_loop" % r, {"p.f": 'print("b")\nxs := [stack(), %s]\nfor (a : xs) { a.push(1) }\n'
                                                % lit})
    for r, lit in receivers.items():
        add("s_field_%s_loop" % r, {"p.f": struct + 'print("b")\nxs := [%s]\nfor (p : xs) { p.v = 1 }\n' % lit})
        add("s_field_%s_mixed" % r, {"p.f": struct + 'print("b")\nxs := [P(), %s]\nfor (p : xs) { p.v = 1 }\n'
                                            % lit})
        add("s_field_%s_param" % r, {"p.f": struct + 'fn(f(p)) { p.v = 1; return 0 }\nprint("b")\nf(P())\n'
                                            'f(%s)\n' % lit})
        add("s_field_%s_mapget" % r, {"p.f": struct + 'print("b")\nm := map()\nm.set("k", %s)\np := m.get("k")\n'
                                             'p.v = 1\n' % lit})
    add("s_field_args", {"p.f": struct + 'print("b")\nfor (p : args) { p.v = 1 }\n'}, argv=["x"])
    return out


def run(nift, name, files, stdin, argv, mode, work):
    d = tempfile.mkdtemp(dir=work)
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "TMPDIR": d, "LANG": "C",
           "LC_ALL": "C", "TZ": "UTC", "NIFT_VM": mode}
    if files is not None:
        for f, text in files.items():
            with open(os.path.join(d, f), "w") as fh:
                fh.write(text)
        cmd = [nift, "p.f", *argv]
        p = subprocess.run([os.environ.get("NIFT_TEST_TIMEOUT", "timeout"), "60", *cmd], cwd=d, env=env, stdin=subprocess.DEVNULL, capture_output=True)
    else:
        cmd = [nift, "-i", *(["-e", "_s := 0", *argv] if argv else [])]
        p = subprocess.run([os.environ.get("NIFT_TEST_TIMEOUT", "timeout"), "60", *cmd], cwd=d, env=env, input=stdin.encode(), capture_output=True)
    norm = lambda b: b.decode("utf-8", "replace").replace(d + os.sep, "")
    shutil.rmtree(d, ignore_errors=True)
    return {"exit": p.returncode, "stdout": norm(p.stdout), "stderr": norm(p.stderr)}


def outcome(r):
    if r["exit"] == 0 and not r["stderr"]:
        return "ok"
    if "no part of it ran" in r["stderr"] or "NIFT_VM session refused" in r["stderr"]:
        return "refuse"
    return "error"


def check_invariants(name, files, r, fail):
    err, out = r["stderr"], r["stdout"]
    if r["exit"] == 70:
        fail("%s: exit 70" % name)
    for t in INTERNAL:
        if t in err:
            fail("%s: internal-failure text %r in stderr" % (name, t))
    kind = outcome(r)
    first = "before\n" if out.startswith("before") else "b\n"
    if kind == "error":
        if not out.startswith(first):
            fail("%s: the earlier output is missing: %r" % (name, out))
        lines = err.splitlines()
        if not lines or not lines[0].startswith("error: "):
            fail("%s: no error line: %r" % (name, err))
        elif files is not None:
            # a script error is positioned: error: FILE:LINE:COL: message
            head = lines[0][len("error: "):].split(":")
            if len(head) < 4 or not head[1].isdigit() or not head[2].isdigit() or int(head[1]) < 1:
                fail("%s: error without a real position: %r" % (name, lines[0]))
            if r["exit"] != 1:
                fail("%s: a script error exits 1, got %d" % (name, r["exit"]))
        elif r["exit"] != 0 or not out.endswith("after\n"):
            fail("%s: a session statement error leaves the session running: exit %d %r" % (name, r["exit"], out))
    elif kind == "refuse":
        if files is not None and (r["exit"] != 1 or out):
            fail("%s: a refusal runs nothing: exit %d stdout %r" % (name, r["exit"], out))
        if "the VM gave no exact position" in err:
            fail("%s: refusal without a position" % name)
    return kind


# Explicit VM-specific correctness oracles (exact bytes and exit status) for cases legacy cannot run: checked on every
# run, --update included, so a reviewed transcript cannot drift. D-073 #6: only the VM-only channel() difference is
# approved; the rest of the case must be exactly this.
VM_ORACLES = {
    "h_channel_json_stringify": {"exit": 1, "stderr": "error: p.f:3:7: undefined callable: json_stringify\n",
                                 "stdout": "b\n"},
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    all_cases = [c for c in cases() if a.only in c[0]]
    names = [c[0] for c in all_cases]
    assert len(names) == len(set(names)), "duplicate case names"
    if a.list:
        print("\n".join(names))
        return 0
    expected = {} if a.update else json.load(open(EXPECTED))
    failures = []
    fail = failures.append
    got, kinds = {}, {"ok": 0, "error": 0, "refuse": 0}
    work = tempfile.mkdtemp(prefix="nift-safety-")
    try:
        for name, files, stdin, argv, legacy in all_cases:
            r = run(nift, name, files, stdin, argv, "require", work)
            kinds[check_invariants(name, files, r, fail)] += 1
            got[name] = r
            if name in VM_ORACLES and VM_ORACLES[name] != r:
                fail("%s: differs from its VM oracle:\n  want %r\n  got  %r" % (name, VM_ORACLES[name], r))
            if legacy:
                lr = run(nift, name, files, stdin, argv, "0", work)
                if lr != r:
                    fail("%s: differs from NIFT_VM=0:\n  vm     %r\n  legacy %r" % (name, r, lr))
            if not a.update:
                want = expected.get(name)
                if want is None:
                    fail("%s: no reviewed transcript in expected.json" % name)
                elif want != r:
                    fail("%s: transcript changed:\n  want %r\n  got  %r" % (name, want, r))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if a.update and not a.only:
        with open(EXPECTED, "w") as fh:
            json.dump(got, fh, indent=1, sort_keys=True)
            fh.write("\n")
    elif not a.only:
        stale = sorted(set(expected) - set(names))
        if stale:
            fail("expected.json has cases the runner no longer generates: %s" % ", ".join(stale[:10]))
    for f in failures:
        print("FAIL " + f)
    summary = "%d cases (%d ordinary errors, %d refusals, %d ok)" % (len(all_cases), kinds["error"],
                                                                      kinds["refuse"], kinds["ok"])
    if failures:
        print("FAIL runtime2 safety: %d problem(s), %s" % (len(failures), summary))
        return 1
    print("PASS runtime2 safety: %s" % summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())
