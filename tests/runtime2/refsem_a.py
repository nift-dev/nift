#!/usr/bin/env python3
"""Reference semantics, Phase A (D-NEXT-refsem): maps, sets, stacks and struct
instances are identity-bearing references on the VM.

  refsem_a.py --nift ./nift            # the suite (make test-runtime2-refsem)
  refsem_a.py --nift ./nift --update   # rewrite the approved/*.vm, *.legacy and equiv/*.vm goldens

Contract: docs/runtime-2/checkpoints/weekend-refsem-a.md.

  parity/*.f    legacy (NIFT_VM=0) and the VM (NIFT_VM=require) give the same
                stdout, stderr and exit status, and the VM ran the program
                (legacy's collections and structs are references too).
  approved/*.f  approved differences (D-NEXT-refsem): the VM output is NAME.vm,
                the legacy witness NAME.legacy, and the two differ.
  refuse/*.f    refused before any effect: exit 1, no stdout (every fixture
                prints first), "no part of it ran", the reason in NAME.reason.
  sessions      piped sessions (SESSIONS below): a copied array is refused
                before it runs; ordinary array use matches legacy; map bindings stay
                refused by the existing session shape contract.
  phaseb/*.f    admitted programs whose meaning Phase B keeps: the VM runs
                them, matches legacy, and prints NAME.phaseb, the transcript
                derived by hand under reference semantics for arrays and
                objects (D-NEXT-refsem #1; the comment in each says why).
  pending/      valid programs Phase A over-refuses (Phase B must admit them):
                NAME.f or NAME.session (piped), NAME.expected the valid output
                (legacy's), NAME.reason today's refusal (pending/README).
  modules/*/    module cases (main.f imports m.f, or a package in .nift/):
                `reason` (refused before any effect), `phaseb` (as phaseb/
                above) or `vm` (an approved difference: the VM's output; `legacy`
                its recorded exit status and error text).
  NAME.args     (parity/, pending/) script arguments for the case.
  equiv/*.f     cooperative (NIFT_VM_COOPERATIVE_THREADS=1) and parallel task
                execution give the same transcript, NAME.vm, both on the VM (D7).
  heap          a refused cycle leaks nothing (4 and 40 refused attempts end
                with the same live heap), and copies, loop snapshots and the
                cooperative spawn copy stop at the heap budget before building.

Exit 0 when every check passes. A mutant binary must make it fail (refsem/mutants.sh).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "refsem")


def run(nift, path, mode, work, budget=None, extra=None, tree=None, args=()):
    d = tempfile.mkdtemp(dir=work)
    name = os.path.basename(path)
    if tree:  # a module case: the whole directory
        for f in os.listdir(tree):  # directories too (a package store, `.nift/`)
            src = os.path.join(tree, f)
            (shutil.copytree if os.path.isdir(src) else shutil.copy)(src, os.path.join(d, f))
    else:
        shutil.copy(path, os.path.join(d, name))
    stats = os.path.join(d, "stats.jsonl")
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "TMPDIR": d, "LANG": "C",
           "LC_ALL": "C", "TZ": "UTC", "NIFT_VM": mode, "NIFT_VM_STATS": stats}
    if budget is not None:
        env["NIFT_VM_HEAP_BUDGET"] = str(budget)
    if extra:
        env.update(extra)
    p = subprocess.run(["timeout", "120", nift, name, *args], cwd=d, env=env, stdin=subprocess.DEVNULL,
                       capture_output=True)
    st = None
    if os.path.exists(stats):
        try:
            st = json.loads(open(stats).read().splitlines()[-1])
        except (OSError, IndexError, ValueError):
            st = None
    norm = lambda b: b.decode("utf-8", "replace").replace(d + os.sep, "")
    return norm(p.stdout), norm(p.stderr), p.returncode, st


def run_session(nift, stdin, work, mode="require", argv=()):
    d = tempfile.mkdtemp(dir=work)
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "TMPDIR": d, "LANG": "C",
           "LC_ALL": "C", "TZ": "UTC", "NIFT_VM": mode}
    p = subprocess.run(["timeout", "120", nift, *argv], cwd=d, env=env, input=stdin.encode(), capture_output=True)
    return p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace"), p.returncode


# Piped sessions (D-NEXT-refsem #9: a later statement may mutate any binding,
# so a copied array, a lending to session code, or a loop over a session
# array is refused; ordinary array use runs).
SESSIONS = [
    # Review round 3 (coordinator): a struct declared in an earlier statement;
    # presenting an array holding an instance is refused before that statement.
    # The earlier statement's print ran; the refused statement printed nothing.
    # Review round 4 (F1): a session binding holding an instance, presented
    # through another name.
    ("session_struct_value", 'struct(C) {\n  v := 0\n}\nc := C()\nx := c\nprint("before")\n'
     'print("x"); print(x.stringify())\n', ("refuse", "before\n", "presentation-struct:stringify")),
    ("session_struct_contents", 'struct(C) {\n  v := 0\n}\nc := C()\na := [c]\nprint("before")\n'
     'print("x"); print(a.stringify())\n', ("refuse", "before\n", "presentation-struct-contents:stringify")),
    ("session_array_copy", 'a := [1]\nb := a\nprint(b.length())\n', ("refuse", "", "reference-guard:inflow")),
    ("session_array_map", 'xs := [3, 1, 2]\nys := xs.map((v) => v * 2)\nprint(ys.length())\n', ("parity",)),
    # Sessions are fully conservative: a session read is typed "anything", so
    # a top-level loop binding over a session array may hold a part of it
    # that a later statement could reach.
    ("session_loop_read", 'a := [1, 2]\nfor (x : a) { print(x) }\n', ("refuse", "", "reference-guard:shared-part")),
    # Review of ad9d842f: a function from an earlier statement mutates the
    # binding the call lends (Phase B: [1,2]; the copy model: [1]).
    ("session_lend_global_callee", 'a := [1]\nfn(f(x)) { a.push(2); print(x.stringify()); return 0 }\nf(a)\n',
     ("refuse", "", "reference-guard:lent")),
    ("session_loop_calls_mutator", 'a := [1]\nfn(g()) { a.push(1); return 0 }\nfor (x : a) { g() }\n',
     ("refuse", "", "reference-guard:shared-part")),
    ("session_array_push", 'a := [1, 2]\na.push(3)\nprint(a.length())\n', ("parity",)),
    # Review round 2: map-typed session bindings were refused because the
    # session symbol's 16-bit kind fields truncated the collection bits (a base
    # defect); with 32-bit fields they run, legacy-equal (maps are references).
    ("session_map_alias", 'm := map()\nb := m\nx := b.set(1, 2)\nprint(m.size())\n', ("parity",)),
    ("session_struct_instance", 'struct(C){v:=0;fn(add(n)){v+=n}}\nc:=C()\nc.add(3)\nprint(c.v)\n', ("parity",)),
    # Review round 2 (cp19 session.cpp 277/278): an Error thrown by an instance
    # method from an earlier statement, caught and saved, keeps its message and
    # source (the 16-bit session kinds turned `c` into a string, so the
    # statement was refused and `saved` kept its seed).
    # B2 review H1: a session function's JSON-spelled object literal is fresh
    # at every call, across statements.
    ("session_literal_fresh", 'fn(f()) {\n  n := 0\n  for ((k, v) : {"a": [1]}) {\n    v.push(5)\n'
     '    n = v.length()\n  }\n  return n\n}\nprint(f())\nprint(f())\n', ("parity",)),
    ("session_saved_error", 'struct(C){v:=0;fn(fail()){throw error("original")}}\nc:=C()\n'
     'saved:=error("seed");try{c.fail()}catch(e){saved=e}\nprint(saved.message)\nprint(saved.source)\n', ("parity",)),
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

    with tempfile.TemporaryDirectory(prefix="refsem-") as work:
        for f in fixtures("parity"):
            name = os.path.basename(f)
            argv = tuple(open(f[:-2] + ".args").read().split()) if os.path.exists(f[:-2] + ".args") else ()
            lo, le, lr, _ = run(nift, f, "0", work, args=argv)
            vo, ve, vr, st = run(nift, f, "require", work, args=argv)
            check(ran_on_vm(st), "parity %s: not run by the VM: %s" % (name, ve.strip()[:200]))
            check((lo, le, lr) == (vo, ve, vr),
                  "parity %s: legacy %r/%r/%d vs VM %r/%r/%d" % (name, lo[-300:], le[-200:], lr, vo[-300:],
                                                                  ve[-200:], vr))
        for f in fixtures("approved"):
            name = os.path.basename(f)
            base = f[:-2]
            vo, ve, vr, st = run(nift, f, "require", work)
            lo, le, lr, _ = run(nift, f, "0", work)
            vm_t, legacy_t = transcript(vo, ve, vr), transcript(lo, le, lr)
            if args.update:
                open(base + ".vm", "w").write(vm_t)
                open(base + ".legacy", "w").write(legacy_t)
                continue
            check(ran_on_vm(st), "approved %s: not run by the VM" % name)
            want_vm = open(base + ".vm").read() if os.path.exists(base + ".vm") else None
            want_legacy = open(base + ".legacy").read() if os.path.exists(base + ".legacy") else None
            check(vm_t == want_vm, "approved %s: VM output\n%s\nexpected\n%s" % (name, vm_t, want_vm))
            check(legacy_t == want_legacy, "approved %s: legacy witness changed\n%s" % (name, legacy_t))
            check(vm_t != legacy_t, "approved %s: no longer differs from legacy" % name)
        for f in fixtures("refuse"):
            name = os.path.basename(f)
            reason = open(f[:-2] + ".reason").read().strip()
            vo, ve, vr, st = run(nift, f, "require", work)
            check(vr == 1 and vo == "", "refuse %s: exit %d stdout %r (must be 1 and empty)" % (name, vr, vo))
            check("(%s)" % reason in ve and "no part of it ran" in ve,
                  "refuse %s: expected reason %s before effects, got %r" % (name, reason, ve.strip()[:300]))
        for f in fixtures("phaseb"):
            name = os.path.basename(f)
            want = open(f[:-2] + ".phaseb").read()
            lo, le, lr, _ = run(nift, f, "0", work)
            vo, ve, vr, st = run(nift, f, "require", work)
            check(ran_on_vm(st) and (vo, ve, vr) == (lo, le, lr) and vo == want,
                  "phaseb %s: VM %r/%r/%d legacy %r/%d, Phase B transcript %r" % (name, vo, ve[:200], vr, lo, lr, want))
        # Review round 4 (F3): valid programs Phase A over-refuses (a temporary
        # limitation Phase B must eliminate). The program is a copy of the
        # original evidence (kept unchanged where it lives); NAME.expected is
        # its valid output (legacy's); today the VM refuses it before any
        # effect with NAME.reason. Phase B turns each into a parity case.
        def extra_of(base, ext, default=None):  # optional NAME.args / NAME.exit / NAME.error / NAME.argv
            return open(base + ext).read() if os.path.exists(base + ext) else default
        for f in fixtures("pending"):
            name = os.path.basename(f)
            reason = open(f[:-2] + ".reason").read().strip()
            want = open(f[:-2] + ".expected").read()
            argv = tuple(extra_of(f[:-2], ".args", "").split())
            want_exit = int(extra_of(f[:-2], ".exit", "0"))
            want_error = (extra_of(f[:-2], ".error", "") or "").strip()
            lo, le, lr, _ = run(nift, f, "0", work, args=argv)
            vo, ve, vr, st = run(nift, f, "require", work, args=argv)
            check(lr == want_exit and lo == want and want_error in le,
                  "pending %s: the preserved legacy behaviour changed: %d %r %r" % (name, lr, lo, le[:200]))
            check(vr == 1 and vo == "" and "(%s)" % reason in ve and "no part of it ran" in ve,
                  "pending %s: expected the Phase A refusal %s before effects (or Phase B support: make it "
                  "a parity case), got %d %r %r" % (name, reason, vr, vo, ve[:200]))
        # The same for piped sessions (NAME.session): the statements before the
        # refused one run (they print nothing here), the refused one has no effect.
        pend = os.path.join(HERE, "pending")
        for name in sorted(f for f in os.listdir(pend) if f.endswith(".session")):
            base = os.path.join(pend, name[:-len(".session")])
            stdin = open(base + ".session").read()
            argv = tuple(json.loads(extra_of(base, ".argv", "[]")))
            reason = open(base + ".reason").read().strip()
            want = open(base + ".expected").read()
            lo, le, lr = run_session(nift, stdin, work, mode="0", argv=argv)
            vo, ve, vr = run_session(nift, stdin, work, argv=argv)
            check(lr == 0 and lo == want, "pending %s: the preserved valid output changed: %r %r" % (name, lo, le[:200]))
            check(vr == 1 and vo == "" and "session refused: %s" % reason in ve,
                  "pending %s: expected the Phase A refusal %s before the statement's effects (or Phase B "
                  "support: make it a parity case), got %d %r %r" % (name, reason, vr, vo, ve[:200]))
        mods = os.path.join(HERE, "modules")
        for case in sorted(os.listdir(mods)):
            tree = os.path.join(mods, case)
            main = os.path.join(tree, "main.f")
            vo, ve, vr, st = run(nift, main, "require", work, tree=tree)
            if os.path.exists(os.path.join(tree, "reason")):
                reason = open(os.path.join(tree, "reason")).read().strip()
                check(vr == 1 and vo == "" and "(%s)" % reason in ve and "no part of it ran" in ve,
                      "module %s: expected %s before effects, got %d %r %r" % (case, reason, vr, vo, ve[:300]))
            elif os.path.exists(os.path.join(tree, "vm")):  # an approved difference (the VM's golden)
                want = open(os.path.join(tree, "vm")).read()
                # legacy's recorded behaviour: `legacy` = exit status, then an error-text substring
                legacy_exit, legacy_error = open(os.path.join(tree, "legacy")).read().split("\n", 1)
                lo, le, lr, _ = run(nift, main, "0", work, tree=tree)
                check(ran_on_vm(st) and vr == 0 and vo == want and lr == int(legacy_exit) and
                      legacy_error.strip() in le,
                      "module %s: VM %r/%r/%d (expected %r), legacy %r/%r/%d (expected exit %s with %r)" %
                      (case, vo, ve[:200], vr, want, lo, le[:200], lr, legacy_exit, legacy_error.strip()))
            else:
                want = open(os.path.join(tree, "phaseb")).read()
                lo, le, lr, _ = run(nift, main, "0", work, tree=tree)
                check(ran_on_vm(st) and (vo, ve, vr) == (lo, le, lr) and vo == want,
                      "module %s: VM %r/%r/%d legacy %r, Phase B %r" % (case, vo, ve[:200], vr, lo, want))
        for f in fixtures("equiv"):
            name = os.path.basename(f)
            base = f[:-2]
            po, pe, pr, pst = run(nift, f, "require", work)
            co, ce, cr, cst = run(nift, f, "require", work, extra={"NIFT_VM_COOPERATIVE_THREADS": "1"})
            par, coop = transcript(po, pe, pr), transcript(co, ce, cr)
            if args.update:
                open(base + ".vm", "w").write(par)
                continue
            check(ran_on_vm(pst) and ran_on_vm(cst), "equiv %s: not run by the VM in both schedulers" % name)
            check(par == coop, "equiv %s: parallel\n%s\ncooperative\n%s" % (name, par, coop))
            want = open(base + ".vm").read() if os.path.exists(base + ".vm") else None
            check(par == want, "equiv %s: output\n%s\nexpected\n%s" % (name, par, want))
        for name, stdin, want in SESSIONS:
            vo, ve, vr = run_session(nift, stdin, work)
            if want[0] == "parity":
                lo, le, lr = run_session(nift, stdin, work, mode="0")
                check((vo, ve, vr) == (lo, le, lr) and "refused" not in ve,
                      "session %s: legacy %r/%r/%d vs VM %r/%r/%d" % (name, lo, le, lr, vo, ve, vr))
            else:
                check(vr == 1 and vo == want[1] and want[2] in ve,
                      "session %s: expected %s, got %d %r %r" % (name, want[2], vr, vo, ve[:200]))
        if args.update:
            print("updated goldens")
            return 0
        heap = os.path.join(HERE, "heap")
        # A refused cycle leaves nothing behind: ten times the refused attempts
        # end with the same live heap (a leaked cycle would accumulate).
        a = run(nift, os.path.join(heap, "cycle_refused.f"), "require", work)
        b = run(nift, os.path.join(heap, "cycle_refused_many.f"), "require", work)
        la = a[3].get("heap_live") if a[3] else None
        lb = b[3].get("heap_live") if b[3] else None
        check(a[2] == 0 and b[2] == 0 and a[0] == "8\n" and b[0] == "80\n" and la is not None and la == lb,
              "heap cycle_refused: exit %d/%d %r/%r, live heap %s != %s (x10): %r" % (a[2], b[2], a[0], b[0], la, lb,
                                                                                     a[1][:200]))
        # The same through captured cells (closures), against a control with
        # no refused store: equal live heaps mean nothing was kept or leaked.
        runs = [run(nift, os.path.join(heap, n), "require", work)
                for n in ("cell_cycle_refused.f", "cell_cycle_refused_many.f", "cell_cycle_control.f")]
        lives = [r[3].get("heap_live") if r[3] else None for r in runs]
        check([r[2] for r in runs] == [0, 0, 0] and [r[0] for r in runs] == ["12\n", "120\n", "12\n"] and
              lives[0] is not None and lives[0] == lives[1] == lives[2],
              "heap cell_cycle_refused: exits %s outputs %s live heaps %s: %r" %
              ([r[2] for r in runs], [r[0] for r in runs], lives, runs[0][1][:200]))
        # Review round 2: the same for every array mutator on a captured cell
        # (insert, splice, expression push, element store, statement push),
        # each followed by reads, mutations and cleanup.
        runs = [run(nift, os.path.join(heap, n), "require", work)
                for n in ("cell_mutator_refused.f", "cell_mutator_refused_many.f", "cell_mutator_control.f")]
        lives = [r[3].get("heap_live") if r[3] else None for r in runs]
        check([r[2] for r in runs] == [0, 0, 0] and [r[0] for r in runs] == ["32\n", "320\n", "32\n"] and
              lives[0] is not None and lives[0] == lives[1] == lives[2],
              "heap cell_mutator_refused: exits %s outputs %s live heaps %s: %r" %
              ([r[2] for r in runs], [r[0] for r in runs], lives, runs[0][1][:200]))
        # Review round 3: a splice removing elements (count > 0) whose
        # replacement closes the cycle removes nothing, in every form.
        runs = [run(nift, os.path.join(heap, n), "require", work)
                for n in ("cell_splice_refused.f", "cell_splice_refused_many.f", "cell_splice_control.f")]
        lives = [r[3].get("heap_live") if r[3] else None for r in runs]
        check([r[2] for r in runs] == [0, 0, 0] and [r[0] for r in runs] == ["56\n", "560\n", "56\n"] and
              lives[0] is not None and lives[0] == lives[1] == lives[2],
              "heap cell_splice_refused: exits %s outputs %s live heaps %s: %r" %
              ([r[2] for r in runs], [r[0] for r in runs], lives, runs[0][1][:200]))
        for fname, budget in (("budget_deepcopy.f", 4_750_000), ("budget_copy.f", 4_750_000),
                              ("budget_snapshot.f", 4_750_000), ("budget_spawn.f", 4_750_000)):
            vo, ve, vr, st = run(nift, os.path.join(heap, fname), "require", work, budget=budget,
                                 extra={"NIFT_VM_COOPERATIVE_THREADS": "1"} if "spawn" in fname else None)
            check(vr == 70 and "memory budget exceeded" in ve and vo == "filled\n",
                  "heap %s: expected the budget stop after `filled` (exit 70), got exit %d %r %r" %
                  (fname, vr, vo, ve[:200]))
            vo, ve, vr, st = run(nift, os.path.join(heap, fname), "require", work,
                                 extra={"NIFT_VM_COOPERATIVE_THREADS": "1"} if "spawn" in fname else None)
            check(vr == 0 and vo.endswith("done\n"), "heap %s: unbounded run failed: %d %r" % (fname, vr, ve[:200]))
    print("%s refsem-a: %d checks, %d failure(s)" % ("PASS" if not failures else "FAIL", count, len(failures)))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
