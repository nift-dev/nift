#!/usr/bin/env python3
"""Slot 3, dead-code admission (owner 2026-11-11 05:41, D-068 #1 and #3; D-071 #4; dead-code-admission.md 5.2).

A call to a name that is no function, binding or built-in is compiled to legacy's `undefined callable: <name>`
raise at the point (src/rt/compiler_deferred.cpp), when the name is certain: no binding form anywhere in the
linked program can supply it (src/rt/name_scan.cpp) and legacy cannot know the name (legacy_known_word).
`if (false) { nope() }` / `print("success")` prints success; reaching `nope()` is the ordinary error.

Each case runs on legacy (NIFT_VM=0) and on the VM (NIFT_VM=require, so a refusal is never hidden by a fallback):
  "vm"            the VM runs it (outcome vm) and equals legacy: exit status, stdout and stderr
  "vm-error"      the VM raises (outcome vm-error) and equals legacy: exit status, stdout and stderr (message,
                  position, `return: ` prefixes per frame, `lambda body error: `, `future: `, earlier output)
  "<reason>"      still refused before it runs, with that reason (name not certain, sessions, exclusions)
Sessions, `nift eval` and the classify-all inventory record (5.4) have their own checks below.

Negative controls: each NIFT_DEADCODE_MUTANT=N / NIFT_NAME_SCAN_MUTANT=N build (deadcode_admission_mutants.py)
must fail every case named for it in MUTANTS.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from fixture_paths import replace_fixture_root


G = 'fn(g(v)) {\n  print("g")\n  return v\n}\n'
SHADOW_FN = 'fn(h()) {\n  return nope()\n}\nfn(m()) {\n  nope := () => "shadow"\n  return h()\n}\nprint(m())\n'
STRUCT_METHOD = ('fn(helper()) {\n  return nope()\n}\nstruct(S) {\n  v := 1\n  fn(nope()) {\n    return 7\n  }\n'
                 '  fn(run()) {\n    return helper()\n  }\n}\ns := S()\nprint(s.run())\n')
STRUCT_FIELD = ('fn(helper()) {\n  if (false) {\n    nope()\n  }\n  return 3\n}\nstruct(S) {\n  nope := 1\n'
                '  fn(run()) {\n    return helper()\n  }\n}\ns := S()\nprint(s.run())\n')
HEADS = {"space": "fn( nope())", "tab": "fn(\tnope())", "newline": "fn(\n    nope())"}
STRUCT_METHOD_HEAD = ('fn(helper()) {\n  return nope()\n}\nstruct(S) {\n  k := 1\n  %s {\n    return 7\n  }\n'
                      '  fn(run()) {\n    return helper()\n  }\n}\ns := S()\nprint(s.run())\n')
TOP_HEAD = '%s {\n  return 5\n}\nif (false) {\n  nope()\n}\nprint(nope())\n'
NESTED_HEAD = ('fn(m()) {\n  %s {\n    return 5\n  }\n  return 0\n}\nfn(g2()) {\n  return nope()\n}\nm()\n'
               'print(g2())\n')
COND_HEAD = 'if (true) {\n  %s {\n    return 5\n  }\n}\nfn(g2()) {\n  return nope()\n}\nprint(g2())\n'
M_DEAD = 'fn(mf(c)) {\n  if (c) {\n    nope()\n  }\n  return 2\n}\nexport(mf)\n'
ASYNC = 'fn[async](af(c)) {\n  if (c) {\n    nope()\n  }\n  return 1\n}\n'

CASES = {
    # Dead code (5.7 matrix): runs, legacy-equal.
    "dead-if": ({"main.f": 'if (false) {\n  nope()\n}\nprint("success")\n'}, "vm"),
    "dead-else": ({"main.f": 'if (true) {\n  print("t")\n} else {\n  nope(1, 2)\n}\n'}, "vm"),
    "dead-uncalled-fn": ({"main.f": 'fn(f()) {\n  return nope()\n}\nprint("ok")\n'}, "vm"),
    "dead-uncalled-fn-stmt": ({"main.f": 'fn(f()) {\n  windows_only()\n  return 1\n}\nprint("ok")\n'}, "vm"),
    "dead-uncalled-lambda": ({"main.f": 'f := () => nope()\nprint("ok")\n'}, "vm"),
    "dead-os-guarded": ({"main.f": 'if (os() == "plan9") {\n  plan9_only()\n} else {\n  print("other")\n}\n'}, "vm"),
    "dead-module-fn": ({"m.nft": M_DEAD, "main.f": 'import("m.nft")\nprint(mf(false))\n'}, "vm"),
    "dead-async": ({"main.f": ASYNC + 'print(await af(false))\n'}, "vm"),
    "dead-short-circuit": ({"main.f": 'print(false && nope())\nprint(true ? 1 : nope())\n'}, "vm"),
    # Reached (5.7 reached variants): legacy's ordinary error, never catchable, no argument evaluated.
    "reach-statement": ({"main.f": 'print("a")\nnope()\nprint("after")\n'}, "vm-error"),
    "reach-args-not-evaluated": ({"main.f": G + 'print("a")\nnope(g(1), g(2))\n'}, "vm-error"),
    "reach-print": ({"main.f": 'print(nope())\n'}, "vm-error"),
    "reach-assignment": ({"main.f": 'x := nope()\nprint(x)\n'}, "vm-error"),
    "reach-operand": ({"main.f": 'print("a" + nope())\n'}, "vm-error"),
    "reach-loop": ({"main.f": 'i := 0\nwhile (i < 1) {\n  i += 1\n  nope()\n}\n'}, "vm-error"),
    "reach-for": ({"main.f": 'for (v : [1, 2]) {\n  print(v)\n  nope()\n}\n'}, "vm-error"),
    "reach-try-not-caught": ({"main.f": 'try {\n  nope()\n} catch (e) {\n  print("caught")\n}\nprint("end")\n'},
                             "vm-error"),
    "reach-fn": ({"main.f": 'fn(f()) {\n  nope()\n  return 1\n}\nprint(f())\n'}, "vm-error"),
    # Statement context (5.7a condition 6): `return: ` at the return statement, one more per calling frame.
    "reach-return": ({"main.f": 'fn(f()) {\n  return nope()\n}\nprint(f())\n'}, "vm-error"),
    "reach-nested-return": ({"main.f": 'fn(f()) {\n  return nope()\n}\nfn(h()) {\n  return f()\n}\nprint(h())\n'},
                            "vm-error"),
    "reach-caller-return": ({"main.f": 'fn(f()) {\n  nope()\n  return 1\n}\nfn(h()) {\n  return f()\n}\n'
                                       'fn(k()) {\n  return h()\n}\nprint(k())\n'}, "vm-error"),
    "reach-return-operand": ({"main.f": 'fn(f()) {\n  return 1 + nope()\n}\nprint(f())\n'}, "vm-error"),
    "reach-lambda": ({"main.f": 'f := () => nope()\nprint(f())\n'}, "vm-error"),
    "reach-lambda-in-return": ({"main.f": 'f := () => nope()\nfn(h()) {\n  return f()\n}\nprint(h())\n'}, "vm-error"),
    "reach-block-lambda-return": ({"main.f": 'f := () => {\n  return nope()\n}\nfn(h()) {\n  return f()\n}\n'
                                             'print(h())\n'}, "vm-error"),
    "reach-module-fn": ({"m.nft": M_DEAD, "main.f": 'import("m.nft")\nprint(mf(true))\n'}, "vm-error"),
    "reach-module-return": ({"m.nft": 'fn(mf()) {\n  return nope()\n}\nexport(mf)\n',
                             "main.f": 'import("m.nft")\nfn(h()) {\n  return mf()\n}\nprint(h())\n'}, "vm-error"),
    "reach-async": ({"main.f": ASYNC + 'try {\n  print(await af(true))\n} catch (e) {\n  print("rejected")\n}\n'},
                    "vm-error"),
    # Name certainty (5.2): any binding of the name anywhere keeps the refusal.
    "shadow-caller-frame": ({"main.f": SHADOW_FN}, "callable:nope"),
    "shadow-parameter": ({"main.f": 'fn(h()) {\n  return nope()\n}\nfn(m(nope)) {\n  return h()\n}\n'
                                    'print(m(() => 7))\n'}, "callable:nope"),
    "struct-method-name": ({"main.f": STRUCT_METHOD}, "callable:nope"),
    "struct-field-name": ({"main.f": STRUCT_FIELD}, "callable:nope"),  # review P1-N
    # Review of C134, F1: a declaration head with whitespace (space, tab, newline) before the name. Legacy prints 7
    # for each struct form; the VM must keep the refusal. The top-level forms compile as functions (legacy-equal);
    # the nested and conditional forms are refused earlier (statement:FnDecl), whatever the scan says.
    **{"struct-method-%s" % k: ({"main.f": STRUCT_METHOD_HEAD % h}, "callable:nope") for k, h in HEADS.items()},
    **{"top-decl-%s" % k: ({"main.f": TOP_HEAD % h}, "vm") for k, h in HEADS.items()},
    **{"nested-decl-%s" % k: ({"main.f": NESTED_HEAD % h}, "statement:FnDecl") for k, h in HEADS.items()},
    **{"cond-decl-%s" % k: ({"main.f": COND_HEAD % h}, "statement:FnDecl") for k, h in HEADS.items()},
    # An imported binding is an ordinary binding: the call compiles as a call of it (no deferral involved).
    "module-binding": ({"m.nft": 'nope := 1\nexport(nope)\n',
                        "main.f": 'import("m.nft")\nif (false) {\n  nope()\n}\nprint("x")\n'}, "vm"),
    "string-mention": ({"main.f": 'print("nope")\nif (false) {\n  nope()\n}\nprint("x")\n'}, "callable:nope"),
    # Unsupported legacy built-ins keep the admission refusal even in dead code.
    "legacy-known-name": ({"main.f": 'if (false) {\n  prique()\n}\nprint("x")\n'}, "callable:prique"),
    "sorted-set-constructor": ({"main.f": 'if (false) {\n  sorted_set()\n}\nprint("x")\n'}, "vm"),
    "legacy-special-name": ({"main.f": 'if (false) {\n  inject("x")\n}\nprint("x")\n'}, "callable:inject"),
    # Exclusions: arguments a compile could refuse (lambda), a return value in loop-evaluator code.
    "argument-lambda": ({"main.f": 'if (false) {\n  nope(() => 1)\n}\nprint("x")\n'}, "callable:nope"),
    "return-in-loop": ({"main.f": 'fn(f()) {\n  i := 0\n  while (i < 1) {\n    i += 1\n    return nope()\n  }\n'
                                  '  return 0\n}\nprint(f())\n'}, "callable:nope"),
    # The 10 S constructs stay early errors (D-068 #5, D-070 #1): one of them, next to a deferred call.
    "static-error-kept": ({"main.f": 'if (false) {\n  nope()\n  break\n}\nprint("x")\n'}, "jump-outside-loop"),
    "static-redeclared-kept": ({"main.f": 'if (false) {\n  nope()\n  x := 1\n  x := 2\n}\nprint("n")\n'},
                               "static-error:redeclared"),
    "static-catch-assign-kept": ({"main.f": 'if (false) {\n  nope()\n  try {\n    throw "a"\n  } catch (e) {\n'
                                            '    e = 1\n  }\n}\nprint("n")\n'}, "assign-catch-binding"),
    "static-await-type-kept": ({"main.f": 'f := () => {\n  nope()\n  x := await\n}\nprint("n")\n'}, "text-type-name"),
    "static-async-call-kept": ({"main.f": 'if (false) {\n  nope()\n  async(f)\n}\nprint("n")\n'},
                               "removed-builtin:async"),
    "static-await-call-kept": ({"main.f": 'if (false) {\n  nope()\n  await(f)\n}\nprint("n")\n'},
                               "removed-builtin:await"),
    "static-struct-body-kept": ({"main.f": 'fn(u()) {\n  nope()\n  struct(S) {\n    x\n  }\n}\nprint("n")\n'},
                                "opaque:struct-body"),
}
# Sessions never defer (5.7a condition 1): the start-up file, stdin, and -e with -i.
SESSION_SRC = 'if (false) {\n  nope()\n}\nprint("st")\n'
MUTANTS = {
    "deadcode-1": ["dead-if", "dead-uncalled-fn", "dead-module-fn", "dead-async", "eval-dead", "inventory-record"],
    "deadcode-2": ["session-startup-file", "session-stdin", "session-e-with-i"],
    "deadcode-3": ["shadow-caller-frame", "shadow-parameter", "struct-method-name", "struct-field-name",
                   "string-mention"],
    "deadcode-4": ["reach-statement", "reach-print", "reach-module-fn", "reach-async", "eval-reach"],
    "deadcode-5": ["reach-args-not-evaluated"],
    "deadcode-6": ["reach-nested-return", "reach-caller-return", "reach-module-return"],
    "name-scan-1": ["struct-method-name"],
    "name-scan-2": ["struct-field-name"],
    "name-scan-3": ["struct-method-space", "struct-method-tab", "struct-method-newline"],  # review of C134, F1/F3
    "deadcode-7": ["legacy-known-name"],  # the legacy-known-name exclusion removed
    "deadcode-8": ["argument-lambda"],  # the inert-argument check removed
    "deadcode-9": ["return-in-loop"],  # the return-in-loop exclusion removed
}


def run(nift, files, mode, args=("main.f",), stdin=None):
    d = tempfile.mkdtemp(prefix="dc-")
    try:
        for rel, text in files.items():
            os.makedirs(os.path.dirname(os.path.join(d, rel)), exist_ok=True)
            with open(os.path.join(d, rel), "w") as f:
                f.write(text)
        st = os.path.join(d, "stats.jsonl")
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "LANG": "C", "NIFT_VM": mode,
               "NIFT_VM_STATS": st}
        p = subprocess.run([os.environ.get("NIFT_TEST_TIMEOUT", "timeout"), "30", nift] + list(args), cwd=d, env=env, capture_output=True,
                           input=stdin.encode() if stdin is not None else None)
        stats = [json.loads(l) for l in open(st)] if os.path.exists(st) else []
        return {"rc": p.returncode, "out": p.stdout, "err": replace_fixture_root(p.stderr, d, "<root>"),
                "stats": stats[-1] if stats else {}}
    finally:
        shutil.rmtree(d, ignore_errors=True)


def check_case(nift, files, want):
    legacy = run(nift, files, "0")
    vm = run(nift, files, "require")
    st = vm["stats"]
    got = st.get("outcome") if st.get("outcome") in ("vm", "vm-error") else st.get("reason", st.get("outcome"))
    problems = []
    if got != want:
        problems.append("outcome %r, expected %r" % (got, want))
    if want in ("vm", "vm-error"):
        for key in ("rc", "out", "err"):
            if legacy[key] != vm[key]:
                problems.append("%s differs: legacy %r vm %r" % (key, legacy[key], vm[key]))
        if vm["rc"] == 70:
            problems.append("exit 70")
    elif vm["rc"] == 0 or vm["out"]:
        problems.append("refusal not before effects: rc %d out %r" % (vm["rc"], vm["out"]))
    return got, problems


def session_checks(nift):
    """name -> problems. Each session input keeps today's refusal (callable:nope) and runs nothing after it."""
    res = {}
    cases = {
        "session-startup-file": ({"st.f": SESSION_SRC}, ("-i", "st.f"), 'print("later")\n'),
        "session-stdin": ({}, ("-i",), SESSION_SRC),
        "session-e-with-i": ({}, ("-i", "-e", 'if (false) { nope() }\nprint("ei")'), 'print(2)\n'),
    }
    for name, (files, args, stdin) in cases.items():
        vm = run(nift, files, "1", args, stdin)
        problems = []
        if vm["rc"] != 1 or b"session refused: callable:nope" not in vm["err"] or vm["out"]:
            problems.append("rc %d out %r err %r" % (vm["rc"], vm["out"], vm["err"][:200]))
        res[name] = problems
    return res


def eval_checks(nift):
    res = {}
    for name, expr, want_rc in (("eval-dead", "false ? nope() : 3", 0), ("eval-reach", "1 + nope()", 2)):
        legacy = run(nift, {}, "0", ("eval", expr))
        vm = run(nift, {}, "require", ("eval", expr))
        problems = []
        if vm["rc"] != want_rc or (legacy["rc"], legacy["out"], legacy["err"]) != (vm["rc"], vm["out"], vm["err"]):
            problems.append("legacy %r vm %r" % ((legacy["rc"], legacy["out"], legacy["err"]),
                                                 (vm["rc"], vm["out"], vm["err"])))
        res[name] = problems
    return res


def inventory_check(nift):
    """5.4: classify-all compiles the unit and lists the deferred site, non-consequential, at its position."""
    vm = run(nift, {"main.f": 'if (false) {\n  print(nope(1))\n}\nprint("n")\n'}, "classify-all")
    st = vm["stats"]
    want = {"reason": "deferred:callable:nope", "line": 2, "column": 9, "consequential": False}
    if st.get("outcome") != "compiled" or want not in (st.get("reasons") or []) or st.get("deferred") != 1:
        return ["classify-all %r" % st]
    return []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    results = {}
    for name, (files, want) in CASES.items():
        got, problems = check_case(nift, files, want)
        results[name] = (got, problems)
    for name, problems in list(session_checks(nift).items()) + list(eval_checks(nift).items()):
        results[name] = ("", problems)
    results["inventory-record"] = ("", inventory_check(nift))
    failures = 0
    for name, (got, problems) in results.items():
        print(("PASS " if not problems else "FAIL ") + name + (" (%s)" % got if got and not problems else ""))
        for p in problems:
            print("    " + p)
        failures += bool(problems)
    print("deadcode-admission: %d/%d pass" % (len(results) - failures, len(results)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
