#!/usr/bin/env python3
"""CP17 x CP13B decision B (owner, 2026-10-27): managed files owned by frames a
caught Error unwinds are released silently, as legacy does.

Legacy (Parser::rollback_file_operation) discards every file created during
an evaluation that fails, so a file a function opened is silently dropped when
an Error thrown in (or below) that function is caught outside it; files that
are still open when the script ends are reported ("managed file left open",
exit 1). Before decision B the combined VM reported the unwound function's
file as left open (exit 1) where legacy exits 0.

The VM rule (the statement rollback, Vm::unwind): the left-open reports of
files created during the aborted evaluation (which includes every frame the
Error unwound) whose last reference died are withdrawn; decision B's separate
per-frame release was removed as redundant (owner, 2026-10-27). W13
admission confines handles to the frame that created them (returns, arguments,
captures, globals, aggregates and throw operands are refused before execution:
the `refused` cases), so a frame's references are its own; same-frame aliases
are cleared in order and only the last one discards.

Modes, all run on the same binary (legacy NIFT_VM=0, VM NIFT_VM=require):
  same          stdout and exit equal legacy; the VM ran (statistics outcome
                vm or vm-error) and its stderr is empty exactly when legacy's is.
  both-reported both exit 1 with "managed file left open" (the try-body local
                and normal-return cases B must NOT silence); stdout equal.
  refused       the VM refuses before execution with host-handle-flow.
  divergence    a classified, reported divergence, pinned so any change is seen
                (none at present: the statement rollback closed the two found
                by decision B's matrix).
Every case also requires: no internal failure (exit 70 / "internal"), and
for files written then abandoned, nothing written to disk in either engine.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

OPEN = 'f := file("p"); f.open("r")'
LEAK = 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\n'
THROW = 'fn(work()) { throw error("stop") }\n'
CASES = [
    ("reproducer", f'fn(work()) {{ {OPEN}; throw error("stop") }}\ntry {{ work() }} catch(e) {{ print(e.message) }}\n', "same"),
    ("closed-before-throw", f'fn(work()) {{ {OPEN}; f.close(); throw error("stop") }}\n'
     'try { work() } catch(e) { print(e.message) }\n', "same"),
    ("never-opened", 'fn(work()) { f := file("p"); throw error("stop") }\ntry { work() } catch(e) { print(e.message) }\n', "same"),
    ("unsaved-write-discarded", 'fn(work()) { f := file("w.txt"); f.open("w"); f.write("data"); throw error("stop") }\n'
     'try { work() } catch(e) { print(e.message) }\nprint(file("w.txt").exists())\n', "same"),
    ("saved-then-unsaved", 'fn(work()) { f := file("w.txt"); f.open("w"); f.write("x"); f.save(); f.write("y"); '
     'throw error("stop") }\ntry { work() } catch(e) { print(e.message) }\n'
     'g := file("w.txt"); g.open("r"); print(g.read_all()); g.close()\n', "same"),
    ("handler-in-caller-frame", f'fn(inner()) {{ {OPEN}; throw error("in") }}\n'
     'fn(work()) { try { inner() } catch(e) { print("caught " + e.message) }; return 2 }\nprint(work())\n', "same"),
    ("thrown-by-callee", f'fn(thrower()) {{ throw error("t") }}\nfn(work()) {{ {OPEN}; thrower() }}\n'
     'try { work() } catch(e) { print(e.message) }\n', "same"),
    ("same-frame-alias", f'fn(work()) {{ {OPEN}; g := f; throw error("stop") }}\n'
     'try { work() } catch(e) { print(e.message) }\n', "same"),
    ("alias-closed", f'fn(work()) {{ {OPEN}; g := f; g.close(); throw error("stop") }}\n'
     'try { work() } catch(e) { print(e.message) }\n', "same"),
    ("recursive-frames", f'fn(work(n)) {{ {OPEN}; if (n > 0) {{ work(n - 1) }} else {{ throw error("deep") }} }}\n'
     'try { work(3) } catch(e) { print(e.message) }\n', "same"),
    ("repeated-then-reuse", f'fn(work()) {{ {OPEN}; throw error("stop") }}\n'
     'n := 0\nwhile (n < 40) { try { work() } catch(e) { }; n = n + 1 }\nprint(n)\n'
     'g := file("p"); g.open("r"); print(g.read_line()); g.close()\n', "same"),
    ("collection-callback", f'fn(work(x)) {{ {OPEN}; throw error("cb") }}\n'
     'try { [1, 2].map(work) } catch(e) { print(e.message) }\n', "same"),
    ("inner-catch-then-throw", f'fn(work()) {{ {OPEN}; try {{ throw error("in") }} catch(e) {{ print("inner " + e.message) }}; '
     'throw error("out") }\ntry { work() } catch(e) { print(e.message) }\n', "same"),
    ("inner-catch-then-close", f'fn(work()) {{ {OPEN}; try {{ throw error("in") }} catch(e) {{ print("inner " + e.message) }}; '
     'f.close(); return 1 }\nprint(work())\n', "same"),
    ("uncaught", f'fn(work()) {{ {OPEN}; throw error("stop") }}\nwork()\n', "same"),
    ("try-body-local", f'try {{ {OPEN}; throw error("body") }} catch(e) {{ print(e.message) }}\n', "both-reported"),
    ("normal-return-leak", f'fn(work()) {{ {OPEN}; return 1 }}\nprint(work())\n', "both-reported"),
    ("leak-then-throw-outer", f'fn(work()) {{ {OPEN}; return 1 }}\nwork()\n'
     'try { throw error("x") } catch(e) { print(e.message) }\n', "both-reported"),
    ("return-handle", f'fn(work()) {{ {OPEN}; return f }}\nx := work()\n', "refused"),
    ("capture-handle", f'fn(work()) {{ {OPEN}; h := () => f.read_all(); throw error("s") }}\n'
     'try { work() } catch(e) { print(e.message) }\n', "refused"),
    ("aggregate-handle", f'fn(work()) {{ {OPEN}; a := [f]; throw error("s") }}\n'
     'try { work() } catch(e) { print(e.message) }\n', "refused"),
    ("outer-handle-in-callee", 'f := file("p")\nfn(work()) { f.open("r"); throw error("s") }\n'
     'try { work() } catch(e) { print(e.message) }\n', "refused"),
    # Statement rollback (owner decision, 2026-10-27): files a callee leaked by
    # RETURNING NORMALLY inside the aborted evaluation are discarded too.
    ("leak-in-unwound-frame", LEAK + 'fn(work()) { leak(); throw error("stop") }\n'
     'try { work() } catch(e) { print(e.message) }\n', "same"),
    ("leak-earlier-in-statement", LEAK + THROW + 'try { x := leak() + work() } catch(e) { print(e.message) }\n', "same"),
    ("leak-in-call-argument", LEAK + THROW + 'fn(id(a, b)) { return a }\n'
     'try { x := id(leak(), work()) } catch(e) { print(e.message) }\n', "same"),
    ("nested-multiple-leaks", LEAK + THROW + 'fn(two()) { leak(); leak(); return 2 }\n'
     f'fn(deep()) {{ two(); {OPEN}; return leak() + work() }}\n'
     'try { deep() } catch(e) { print(e.message) }\n', "same"),
    ("leak-closed-before-throw", 'fn(lc()) { g := file("q"); g.open("r"); g.close(); return 1 }\n' + THROW +
     'try { x := lc() + work() } catch(e) { print(e.message) }\n', "same"),
    ("handler-in-function", LEAK + THROW + 'fn(h()) { try { x := leak() + work() } catch(e) { print(e.message) }; return 0 }\n'
     'print(h())\n', "same"),
    ("caught-by-outer-from-catch-block", LEAK + THROW +
     'try { try { throw error("a") } catch(e) { x := leak() + work() } } catch(e) { print("outer " + e.message) }\n',
     "same"),
    ("assignment-statement", LEAK + THROW + 'try { x := 1; x = leak() + work() } catch(e) { print(e.message) }\n', "same"),
    ("repeated-statement-rollback", LEAK + THROW +
     'n := 0\nwhile (n < 40) { try { x := leak() + work() } catch(e) { }; n = n + 1 }\nprint(n)\n', "same"),
    ("uncaught-statement", LEAK + THROW + 'x := leak() + work()\n', "same"),
    ("stream-leak-in-statement", 'fn(wstream()) { o := ofstream("out.txt"); o.write("abc"); return 1 }\n' + THROW +
     'try { x := wstream() + work() } catch(e) { print(e.message) }\n', "same"),
    # Boundaries legacy keeps: an evaluation that COMPLETED stays reported.
    ("owner-example-separate-statements", 'fn(open_file()) {\n    f := file("p")\n    f.open("r")\n}\n'
     'try {\n    open_file()\n    throw error("stop")\n} catch(e) {\n    print(e.message)\n}\n', "both-reported"),
    ("if-condition-completed", LEAK + THROW + 'try { if (leak() > 0) { work() } } catch(e) { print(e.message) }\n',
     "both-reported"),
    # Equivalence of the single rollback with decision B (owner, 2026-10-27).
    ("leak-in-catch-then-rethrow", LEAK + 'fn(w()) { try { throw error("a") } catch(e) { leak(); throw error("b") } }\n'
     'try { w() } catch(e) { print(e.message) }\n', "same"),
    ("nested-try-completed-then-throw", LEAK + THROW +
     'try { try { leak(); throw error("a") } catch(e) { print("inner") }; work() } catch(e) { print(e.message) }\n',
     "both-reported"),
    ("mixed-depths", LEAK + 'fn(d2()) { g := file("p"); g.open("r"); return 1 }\n'
     'fn(d1()) { d2(); f := file("p"); f.open("r"); throw error("x") }\n'
     'try { leak(); d1() } catch(e) { print(e.message) }\n', "both-reported"),
    ("mixed-depths-one-statement", LEAK + 'fn(d2()) { g := file("p"); g.open("r"); return 1 }\n'
     'fn(d1()) { d2(); f := file("p"); f.open("r"); throw error("x") }\n'
     'try { x := leak() + d1() } catch(e) { print(e.message) }\n', "same"),
    ("if-condition-after-leak-statement", LEAK + THROW +
     'try { leak(); if (work() > 0) { print("no") } } catch(e) { print(e.message) }\n', "both-reported"),
    ("else-if-condition-completed", LEAK + THROW +
     'try { if (leak() < 0) { print("no") } else if (work() > 0) { print("no") } } catch(e) { print(e.message) }\n',
     "both-reported"),
    ("while-condition-completed", LEAK + THROW +
     'i := 0\ntry { while (leak() > 0 && i < 1) { i = i + 1; work() } } catch(e) { print(e.message) }\n',
     "both-reported"),
    ("while-condition-after-body-leak", LEAK + THROW +
     'i := 0\ntry { while (i < 1 || work() > 0) { i = i + 1; leak() } } catch(e) { print(e.message) }\n',
     "both-reported"),
    ("while-body-earlier-statement", LEAK + THROW +
     'i := 0\ntry { while (i < 2) { leak(); i = i + 1 }; work() } catch(e) { print(e.message) }\n', "both-reported"),
    ("earlier-statement-in-try", LEAK + 'try { leak(); throw error("x") } catch(e) { print(e.message) }\n',
     "both-reported"),
]


def run(nift, root, source, vm):
    (root / "case.f").write_text(source)
    for name in ("w.txt", "out.txt", "stats.jsonl"):
        if (root / name).exists():
            (root / name).unlink()
    env = dict(os.environ)
    env["NIFT_VM"] = "require" if vm else "0"
    env.pop("NIFT_VM_STATS", None)
    env["NIFT_HANDLE_LIMIT"] = "8"  # repeated-then-reuse: 40 unwound files must all be reclaimed
    if vm:
        env["NIFT_VM_STATS"] = str(root / "stats.jsonl")
    proc = subprocess.run([nift, "case.f"], cwd=root, env=env, stdin=subprocess.DEVNULL,
                          capture_output=True, text=True, timeout=60)
    record = {}
    if vm and (root / "stats.jsonl").exists():
        lines = [json.loads(line) for line in (root / "stats.jsonl").read_text().splitlines() if line.strip()]
        record = lines[-1] if lines else {}
    written = tuple((root / n).read_text() if (root / n).exists() else None for n in ("w.txt", "out.txt"))
    return proc.stdout, proc.stderr, proc.returncode, record, written


def reported(err):
    """The file names a left-open report names (legacy and the VM word it the same way)."""
    import re
    return sorted(Path(m).name for m in re.findall(r"managed file left open(?: \(\d+ files\))?: (\S+)", err))


def check(mode, legacy, vm):
    lo, le, lrc, _, lw = legacy
    vo, ve, vrc, st, vw = vm
    if vrc == 70 or "internal" in ve.lower():
        return [f"VM internal failure rc={vrc} err={ve!r}"]
    outcome = st.get("outcome")
    if mode == "refused":
        return [] if outcome == "require-error" and st.get("reason") == "host-handle-flow" else \
            [f"expected host-handle-flow refusal, got {outcome}/{st.get('reason')}"]
    if outcome not in ("vm", "vm-error"):
        return [f"not executed on the VM: {outcome}/{st.get('reason')}"]
    problems = []
    if lw != vw:
        problems.append(f"file contents differ: legacy {lw!r} vm {vw!r}")
    if mode == "same":
        if (lo, lrc) != (vo, vrc) or (le == "") != (ve == ""):
            problems.append(f"legacy {(lo, lrc, le)!r} != vm {(vo, vrc, ve)!r}")
        if "left open" in ve:
            problems.append(f"VM reported an unwound file: {ve!r}")
    elif mode == "both-reported":
        if not (lrc == vrc == 1 and lo == vo and "managed file left open" in le and "managed file left open" in ve):
            problems.append(f"both must report: legacy {(lo, lrc, le)!r} vm {(vo, vrc, ve)!r}")
        elif reported(le) != reported(ve):
            problems.append(f"different files reported: legacy {reported(le)} vm {reported(ve)}")
    elif mode == "divergence":
        if not (lrc == 0 and le == "" and vrc == 1 and "managed file left open" in ve and lo == vo):
            problems.append(f"classified divergence changed: legacy {(lo, lrc, le)!r} vm {(vo, vrc, ve)!r}")
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    if not os.access(nift, os.X_OK):
        print(f"FAIL: no executable {nift}")
        return 1
    failures = divergences = 0
    for name, source, mode in CASES:
        with tempfile.TemporaryDirectory(prefix="cp17-unwind-") as tmp:
            root = Path(tmp)
            (root / "p").write_text("one\ntwo\n")
            (root / "q").write_text("q\n")
            legacy = run(nift, root, source, False)
            vm = run(nift, root, source, True)
            problems = check(mode, legacy, vm)
            if problems:
                failures += 1
                print(f"FAIL {name} ({mode}): " + "; ".join(problems))
            else:
                divergences += mode == "divergence"
                print(f"ok   {name} ({mode})")
    print(f"{'PASS' if not failures else 'FAIL'} cp17 unwind handle release (decision B): {len(CASES)} cases, "
          f"{failures} failure(s); {divergences} classified divergence(s) reported, not accepted")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
