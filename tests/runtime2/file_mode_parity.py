#!/usr/bin/env python3
"""B02 (combined-b3-b5.md section 46): file() buffer methods by open state, the VM against legacy.

Every file() method the VM runs is called in every open state:
  never   file("data") never opened
  r w a rw  opened in that mode (data holds "ABCDEFGH\n" first)
  closed  opened "rw", then closed
in two programs: plain (BEFORE, the call, AFTER, a cleanup that cannot fail, END) and try (the
call inside try/catch, then the buffer state it left: the modified flag and, where readable, the
contents). Each runs on the VM (NIFT_VM=require) and on legacy (NIFT_VM=0); exit status, stdout,
stderr and every file's bytes must be equal, so a refused operation that changed the buffer or
the file before failing is caught. The cells an approved decision covers (APPROVED, with its
citation) must show exactly the approved VM result instead.
Legacy's mode preconditions (src/ParserExpression.cpp execute_file_method): need_open (modified,
tell, seek, close), need_read (read, read_all, read_line, eof), need_write (write, write_line,
flush, revert, save), closed only (copy, move, remove), none (path, exists).

  --print-table   print the mode x method table (VM result per cell: = legacy, or the difference)

Usage: python3 -B tests/runtime2/file_mode_parity.py --nift ./nift [--print-table]
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

STATES = {
    "never": "",
    "r": 'f.open("r")',
    "w": 'f.open("w")',
    "a": 'f.open("a")',
    "rw": 'f.open("rw")',
    "closed": 'f.open("rw")\nf.close()',
}
CALLS = {
    "open": 'f.open("r")', "close": "f.close()", "read": "f.read()", "read(2)": "f.read(2)",
    "read_all": "f.read_all()", "read_line": "f.read_line()", "eof": "f.eof()",
    "write": 'f.write("xy")', "write_line": 'f.write_line("xy")', "flush": "f.flush()",
    "revert": "f.revert()", "save": "f.save()", "modified": "f.modified()", "tell": "f.tell()",
    "seek(1)": "f.seek(1)", "path": "f.path()", "exists": "f.exists()", "copy": 'f.copy("copied")',
    "move": 'f.move("moved")', "remove": "f.remove()",
    # C56 (review of C53-C55, F-1): non-string values through write and write_line.
    "write([1])": "f.write([1])", "write_line([1])": "f.write_line([1])",
    'write({"k":1})': 'f.write({"k":1})', 'write_line({"k":1})': 'f.write_line({"k":1})',
    "write(7)": "f.write(7)", "write_line(2.5)": "f.write_line(2.5)",
    "write(true)": "f.write(true)", "write_line(null)": "f.write_line(null)",
}
STATEMENT = {"copy", "move"}  # return a file value: called as a statement, not printed
CLOSED = "handle is closed (file handle 0/1, runtime.handle_closed)"
D039 = "D-039 (owner 2026-10-20): after close(), later operations raise runtime.handle_closed (catchable)"
# The cells an approved decision covers: every call on a closed file (D-039), and an operation
# after close() in the try variant's observation (D-039 too: catchable handle_closed there, where
# legacy's "file is not open" is fatal).
APPROVED = {("closed", c): D039 for c in CALLS}
APPROVED.update({(st, "close"): D039 for st in ("r", "w", "a", "rw")})
REFUSED = "not compiled for the VM (host-handle-flow)"
# copy() and move() return a file value: the VM refuses the program before it runs (a B27
# refusal: nothing runs, no file changes), never a different result.
REFUSAL = {(st, c) for st in STATES for c in ("copy", "move")}


def cleanup(state, call):
    """Statements that leave no file open and cannot fail, after `call` succeeded in `state`."""
    if call in ("close", "move", "remove"):
        return ""
    if state in ("w", "a", "rw"):
        return "f.revert()\nf.close()\n"
    if state == "r" or (state == "never" and call == "open"):
        return "f.close()\n"
    return ""


def programs(state, call):
    """plain: the call, then AFTER. try: the call inside try/catch, then the buffer state it left
    (modified flag, and the contents where the state can read them), each observation guarded."""
    setup, text = STATES[state], CALLS[call]
    shown = text if call in STATEMENT else "print(%s)" % text
    tail = cleanup(state, call) + 'print("END")\n'
    plain = 'f:=file("data")\n%s\nprint("BEFORE")\n%s\nprint("AFTER")\n%s' % (setup, shown, tail)
    observe = 'try { print(f.modified()) } catch(e) { print("M " + e.message) }\n'
    if state in ("r", "rw"):
        observe += 'try { f.seek(0); print(f.read_all()) } catch(e) { print("R " + e.message) }\n'
    guarded = ('f:=file("data")\n%s\nprint("BEFORE")\ntry { %s } catch(e) { print("CAUGHT " + e.message) }\n'
               '%sprint("AFTER")\n%s' % (setup, shown, observe, tail))
    return {"plain": plain, "try": guarded}


def run(nift, engine, source, work):
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir()
    (work / "data").write_bytes(b"ABCDEFGH\n")
    (work / "case.f").write_text(source)
    env = dict(os.environ, NIFT_VM=engine)
    p = subprocess.run([nift, "case.f"], cwd=work, env=env, capture_output=True, text=True, timeout=60)
    files = {f.name: f.read_bytes() for f in sorted(work.iterdir()) if f.name != "case.f"}
    return p.returncode, p.stdout, p.stderr.replace(str(work), "$W"), files


def message(stderr):
    """The error text after "error: $W/case.f:L:C: " (or the whole stderr)."""
    line = stderr.splitlines()[0] if stderr else ""
    head = "error: $W/case.f:"
    if line.startswith(head):
        return line[len(head):].split(": ", 1)[-1], line[len(head):].split(": ", 1)[0]
    return line, ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--print-table", action="store_true")
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    fails = 0
    table = {}
    with tempfile.TemporaryDirectory(prefix="file-mode-parity-") as tmp:
        work = Path(tmp) / "w"
        for state in STATES:
            for call in CALLS:
                cell = "%s/%s" % (state, call)
                marks = []
                for variant, source in programs(state, call).items():
                    vm = run(nift, "require", source, work)
                    legacy = run(nift, "0", source, work)
                    if (state, call) in REFUSAL:
                        ok = (vm[0] == 1 and vm[1] == "" and REFUSED in vm[2] and vm[3] == {"data": b"ABCDEFGH\n"})
                        label, mark = "refused before running (host-handle-flow, B27)", "refused"
                    elif (state, call) in APPROVED and vm != legacy:
                        # D-039: the VM raises handle_closed at the call (caught in the try variant);
                        # nothing else differs: no file bytes change.
                        if call == "close":  # idempotent close; uses after it: handle_closed, caught
                            ok = vm[0] == 0 and vm[1].startswith("BEFORE\nnull\n") and vm[2] == "" and (
                                variant == "plain" or ("M modified: " + CLOSED) in vm[1])
                        elif variant == "plain":
                            ok = vm[0] == 1 and vm[1] == "BEFORE\n" and message(vm[2])[0].endswith(CLOSED)
                        else:
                            ok = vm[0] == 0 and ("CAUGHT %s: %s" % (call.split("(")[0], CLOSED)) in vm[1]
                        ok = ok and vm[3] == {"data": b"ABCDEFGH\n"}
                        label, mark = "approved (%s)" % APPROVED[(state, call)], "D-039"
                    else:
                        ok = vm == legacy
                        label, mark = "VM == legacy (exit, stdout, stderr, file bytes)", "=" if ok else "DIFF"
                    marks.append(mark)
                    if ok:
                        print("PASS %s [%s]: %s" % (cell, variant, label))
                    else:
                        fails += 1
                        print("FAIL %s [%s]: %s\n  VM     %r\n  legacy %r" % (cell, variant, label, vm, legacy))
                table[cell] = marks[0] if marks[0] == marks[1] else "/".join(marks)
    if a.print_table:
        calls = list(CALLS)
        print("| state | " + " | ".join(calls) + " |")
        print("| --- |" + " --- |" * len(calls))
        for state in STATES:
            print("| %s | " % state + " | ".join(table["%s/%s" % (state, c)] for c in calls) + " |")
    total = len(STATES) * len(CALLS)
    print(("FAIL" if fails else "PASS") + " file mode parity: %d cells x 2 programs, %d failure(s)" % (total, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
