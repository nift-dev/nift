#!/usr/bin/env python3
"""CP17 x CP13B decision B (owner, 2026-10-27): managed files owned by frames a
caught Error unwinds are released silently, as legacy does.

Legacy (Parser::rollback_file_operation) discards every file created during
an evaluation that fails, so a file a function opened is silently dropped when
an Error thrown in (or below) that function is caught outside it; files that
are still open when the script ends are reported ("managed file left open",
exit 1). Before decision B the combined VM reported the unwound function's
file as left open (exit 1) where legacy exits 0.

The VM rule (natives::abandon_file_on_unwind, called from Vm::unwind for each
register of each popped frame before it is cleared): the last reference to an
open file owned by the run is discarded without writing or reporting. W13
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
  divergence    a classified, reported divergence, pinned so any change is seen:
                legacy forgives a file leaked by a function that RETURNED
                normally earlier in the same failing statement; the VM reports
                it (the leaking frame was not unwound). Owner-visible
                qualification, not silently accepted.
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
    ("leak-in-unwound-frame", 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\n'
     'fn(work()) { leak(); throw error("stop") }\ntry { work() } catch(e) { print(e.message) }\n', "divergence"),
    ("leak-earlier-in-statement", 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\n'
     'fn(work()) { throw error("stop") }\ntry { x := leak() + work() } catch(e) { print(e.message) }\n', "divergence"),
]


def run(nift, root, source, vm):
    (root / "case.f").write_text(source)
    for name in ("w.txt", "stats.jsonl"):
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
    written = (root / "w.txt").read_text() if (root / "w.txt").exists() else None
    return proc.stdout, proc.stderr, proc.returncode, record, written


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
