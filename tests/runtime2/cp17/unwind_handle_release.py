#!/usr/bin/env python3
"""CP17 unwind release: managed files and caught Errors (owner decision,
2026-10-28; an approved Runtime 2.0 semantic correction that replaces decision
B and the statement rollback).

Contract. When a caught Error unwinds code, an open managed file whose last
reference belonged to the unwound code (a frame it pops, or the try body or
catch body it aborts in the handler's frame) is closed silently: unsaved
changes are discarded, never written, and nothing is reported (the aborted
code could not reach its close). A file abandoned by code that completed -- a
normal return, a finished statement, block or loop iteration -- is reported
("managed file left open", "(N files)" for several), exit 1, wherever it
happened: loops, prepared or not, functions, lambdas, methods and callbacks
alike. A file still referenced by surviving code (an outer binding, an alias)
is left open and usable. An uncaught Error stays the run's error.

Legacy is evidence, not the specification: it rolls back failed textual
evaluations (Parser::rollback_file_operation) but not its prepared (loop)
evaluator's, an evaluator-boundary quirk the VM does not reproduce.

Each case states the reports the VM must make, as (file name, count) pairs,
and its relation to legacy (same binary, NIFT_VM=0 against NIFT_VM=require):
  legacy        legacy behaves identically: stdout, exit status, stderr
                emptiness, reports and written files all equal.
  correction    an approved difference: the VM makes the stated reports and
                legacy does not (pinned, so a change on either side is seen);
                stdout and written files still equal legacy's.
  refused       the VM refuses before execution with host-handle-flow (W13).
Every case requires the VM to have run (statistics outcome vm or vm-error),
no internal failure, and exit status 1 whenever it reports (an uncaught Error
also exits 1, as the run's error).
With several files the named one is the VM's most recently created; legacy's
choice is its hash table's order, which is not part of the contract, so for a
`legacy` case the name must match legacy's (same platform) and for a
`correction` case the stated name is the VM's.
The `m-` cases are the evaluation-boundary matrix the rework derived from
legacy, the `rev-` cases the independent review's F1 programs (each of which
showed the earlier statement rollback losing a report legacy keeps), the `c-`
cases the contract's scope boundaries (the *-throw-first cases throw in a later
iteration before the body redeclares its file, so only the iteration-end clear
reports the earlier iteration's file; in the while cases two declarations
precede the file so that the throw's temporaries cannot reuse its register
and drop it, which would hide a missing clear).
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

CASES = [
    ('c-while-normal-completion', 'i := 0\nwhile (i < 3) { i = i + 1; f := file("p"); f.open("r") }\nprint(i)\n', 'legacy', [('p', 3)]),
    ('c-for-normal-completion', 'for (v : [1, 2]) { f := file("p"); f.open("r") }\nprint("done")\n', 'legacy', [('p', 2)]),
    ('c-alias-survives-loop', 'g := file("p")\ng.open("r")\ni := 0\nwhile (i < 2) { i = i + 1; f := g; print(f.read_line()) }\nprint(g.eof())\ng.close()\n', 'legacy', []),
    ('c-alias-survives-loop-throw', 'g := file("p")\ng.open("r")\ni := 0\ntry { while (i < 2) { i = i + 1; f := g; if (i == 2) { throw error("x") } } } catch(e) { print(e.message) }\nprint(g.read_line())\ng.close()\n', 'legacy', []),
    ('c-while-throw-first', 'i := 0\ntry { while (i < 2) { i = i + 1; if (i == 2) { throw error("x") }; pad := 0; pad2 := 0; f := file("p"); f.open("r") } } catch(e) { print(e.message) }\n', 'legacy', [('p', 1)]),
    ('c-for-throw-first', 'try { for (v : [1, 2]) { if (v == 2) { throw error("x") }; f := file("p"); f.open("r") } } catch(e) { print(e.message) }\n', 'legacy', [('p', 1)]),
    ('c-continue-throw-first', 'i := 0\ntry { while (i < 3) { i = i + 1; if (i == 3) { throw error("x") }; pad := 0; pad2 := 0; f := file("p"); f.open("r"); if (i < 3) { continue } } } catch(e) { print(e.message) }\n', 'legacy', [('p', 2)]),
    ('c-for-continue-throw-first', 'try { for (v : [1, 2, 3]) { if (v == 3) { throw error("x") }; f := file("p"); f.open("r"); if (v < 3) { continue } } } catch(e) { print(e.message) }\n', 'legacy', [('p', 2)]),
    ('c-two-completed-names', 'fn(a()) { f := file("p"); f.open("r"); return 1 }\nfn(b()) { g := file("q"); g.open("r"); return 1 }\nprint(a() + b())\n', 'legacy', [('q', 2)]),
    ('c-for-break-then-throw', 'try { for (v : [1, 2]) { f := file("p"); f.open("r"); break }; throw error("x") } catch(e) { print(e.message) }\n', 'legacy', [('p', 1)]),
    ('reproducer', 'fn(work()) { f := file("p"); f.open("r"); throw error("stop") }\ntry { work() } catch(e) { print(e.message) }\n', 'legacy', []),
    ('closed-before-throw', 'fn(work()) { f := file("p"); f.open("r"); f.close(); throw error("stop") }\ntry { work() } catch(e) { print(e.message) }\n', 'legacy', []),
    ('never-opened', 'fn(work()) { f := file("p"); throw error("stop") }\ntry { work() } catch(e) { print(e.message) }\n', 'legacy', []),
    ('unsaved-write-discarded', 'fn(work()) { f := file("w.txt"); f.open("w"); f.write("data"); throw error("stop") }\ntry { work() } catch(e) { print(e.message) }\nprint(file("w.txt").exists())\n', 'legacy', []),
    ('saved-then-unsaved', 'fn(work()) { f := file("w.txt"); f.open("w"); f.write("x"); f.save(); f.write("y"); throw error("stop") }\ntry { work() } catch(e) { print(e.message) }\ng := file("w.txt"); g.open("r"); print(g.read_all()); g.close()\n', 'legacy', []),
    ('handler-in-caller-frame', 'fn(inner()) { f := file("p"); f.open("r"); throw error("in") }\nfn(work()) { try { inner() } catch(e) { print("caught " + e.message) }; return 2 }\nprint(work())\n', 'legacy', []),
    ('thrown-by-callee', 'fn(thrower()) { throw error("t") }\nfn(work()) { f := file("p"); f.open("r"); thrower() }\ntry { work() } catch(e) { print(e.message) }\n', 'legacy', []),
    ('same-frame-alias', 'fn(work()) { f := file("p"); f.open("r"); g := f; throw error("stop") }\ntry { work() } catch(e) { print(e.message) }\n', 'legacy', []),
    ('alias-closed', 'fn(work()) { f := file("p"); f.open("r"); g := f; g.close(); throw error("stop") }\ntry { work() } catch(e) { print(e.message) }\n', 'legacy', []),
    ('recursive-frames', 'fn(work(n)) { f := file("p"); f.open("r"); if (n > 0) { work(n - 1) } else { throw error("deep") } }\ntry { work(3) } catch(e) { print(e.message) }\n', 'legacy', []),
    ('repeated-then-reuse', 'fn(work()) { f := file("p"); f.open("r"); throw error("stop") }\nn := 0\nwhile (n < 40) { try { work() } catch(e) { }; n = n + 1 }\nprint(n)\ng := file("p"); g.open("r"); print(g.read_line()); g.close()\n', 'legacy', []),
    ('collection-callback', 'fn(work(x)) { f := file("p"); f.open("r"); throw error("cb") }\ntry { [1, 2].map(work) } catch(e) { print(e.message) }\n', 'legacy', []),
    ('inner-catch-then-throw', 'fn(work()) { f := file("p"); f.open("r"); try { throw error("in") } catch(e) { print("inner " + e.message) }; throw error("out") }\ntry { work() } catch(e) { print(e.message) }\n', 'legacy', []),
    ('inner-catch-then-close', 'fn(work()) { f := file("p"); f.open("r"); try { throw error("in") } catch(e) { print("inner " + e.message) }; f.close(); return 1 }\nprint(work())\n', 'legacy', []),
    ('uncaught', 'fn(work()) { f := file("p"); f.open("r"); throw error("stop") }\nwork()\n', 'legacy', []),
    ('try-body-local', 'try { f := file("p"); f.open("r"); throw error("body") } catch(e) { print(e.message) }\n', 'correction', []),
    ('normal-return-leak', 'fn(work()) { f := file("p"); f.open("r"); return 1 }\nprint(work())\n', 'legacy', [('p', 1)]),
    ('leak-then-throw-outer', 'fn(work()) { f := file("p"); f.open("r"); return 1 }\nwork()\ntry { throw error("x") } catch(e) { print(e.message) }\n', 'legacy', [('p', 1)]),
    ('return-handle', 'fn(work()) { f := file("p"); f.open("r"); return f }\nx := work()\n', "refused", None),
    ('capture-handle', 'fn(work()) { f := file("p"); f.open("r"); h := () => f.read_all(); throw error("s") }\ntry { work() } catch(e) { print(e.message) }\n', "refused", None),
    ('aggregate-handle', 'fn(work()) { f := file("p"); f.open("r"); a := [f]; throw error("s") }\ntry { work() } catch(e) { print(e.message) }\n', "refused", None),
    ('outer-handle-in-callee', 'f := file("p")\nfn(work()) { f.open("r"); throw error("s") }\ntry { work() } catch(e) { print(e.message) }\n', "refused", None),
    ('leak-in-unwound-frame', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { leak(); throw error("stop") }\ntry { work() } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('leak-earlier-in-statement', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { x := leak() + work() } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('leak-in-call-argument', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(id(a, b)) { return a }\ntry { x := id(leak(), work()) } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('nested-multiple-leaks', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(two()) { leak(); leak(); return 2 }\nfn(deep()) { two(); f := file("p"); f.open("r"); return leak() + work() }\ntry { deep() } catch(e) { print(e.message) }\n', 'correction', [('q', 3)]),
    ('leak-closed-before-throw', 'fn(lc()) { g := file("q"); g.open("r"); g.close(); return 1 }\nfn(work()) { throw error("stop") }\ntry { x := lc() + work() } catch(e) { print(e.message) }\n', 'legacy', []),
    ('handler-in-function', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { try { x := leak() + work() } catch(e) { print(e.message) }; return 0 }\nprint(h())\n', 'correction', [('q', 1)]),
    ('caught-by-outer-from-catch-block', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { try { throw error("a") } catch(e) { x := leak() + work() } } catch(e) { print("outer " + e.message) }\n', 'correction', [('q', 1)]),
    ('assignment-statement', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { x := 1; x = leak() + work() } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('repeated-statement-rollback', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nn := 0\nwhile (n < 40) { try { x := leak() + work() } catch(e) { }; n = n + 1 }\nprint(n)\n', 'correction', [('q', 40)]),
    ('uncaught-statement', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nx := leak() + work()\n', 'legacy', []),
    ('stream-leak-in-statement', 'fn(wstream()) { o := ofstream("out.txt"); o.write("abc"); return 1 }\nfn(work()) { throw error("stop") }\ntry { x := wstream() + work() } catch(e) { print(e.message) }\n', 'legacy', []),
    ('owner-example-separate-statements', 'fn(open_file()) {\n    f := file("p")\n    f.open("r")\n}\ntry {\n    open_file()\n    throw error("stop")\n} catch(e) {\n    print(e.message)\n}\n', 'legacy', [('p', 1)]),
    ('if-condition-completed', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { if (leak() > 0) { work() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('if-condition-after-leak-statement', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { leak(); if (work() > 0) { print("no") } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('else-if-condition-completed', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { if (leak() < 0) { print("no") } else if (work() > 0) { print("no") } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('while-condition-completed', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (leak() > 0 && i < 1) { i = i + 1; work() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('while-condition-after-body-leak', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1 || work() > 0) { i = i + 1; leak() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('while-body-earlier-statement', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 2) { leak(); i = i + 1 }; work() } catch(e) { print(e.message) }\n', 'legacy', [('q', 2)]),
    ('earlier-statement-in-try', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\ntry { leak(); throw error("x") } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-t-stmt', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { x := leak() + work() } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-t-earlier-stmt', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { leak(); work() } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-t-if-cond', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { if (leak() + work() > 0) { print("n") } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-w-cond', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { while (leak() + work() > 0) { print("n") } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-body-decl', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; x := leak() + work() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-body-assign', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\nx := 0\ntry { while (i < 1) { i = i + 1; x = leak() + work() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-body-compound', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\nx := 0\ntry { while (i < 1) { i = i + 1; x += leak() + work() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-body-callargs', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(id(a, b)) { return a }\ni := 0\ntry { while (i < 1) { i = i + 1; id(leak(), work()) } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-body-print', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; print(leak() + work()) } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-body-earlier', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; leak(); work() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-body-if-cond', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; if (leak() + work() > 0) { print("n") } } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-body-elseif-cond', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; if (false) { print("n") } else if (leak() + work() > 0) { print("n") } } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-nested-while', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; j := 0; while (j < 1) { j = j + 1; x := leak() + work() } } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-in-if', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { if (true) { while (i < 1) { i = i + 1; x := leak() + work() } } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-break-cont', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 3) { i = i + 1; if (i == 1) { continue }; x := leak() + work(); break } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-unwound-frame', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(w()) { f := file("p"); f.open("r"); throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; w() } } catch(e) { print(e.message) }\n', 'legacy', []),
    ('m-w-unwound-completed-callee', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { leak(); work() }\ni := 0\ntry { while (i < 1) { i = i + 1; h() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-unwound-completed-callee-decl', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { leak(); work(); return 1 }\ni := 0\ntry { while (i < 1) { i = i + 1; x := leak2() + h() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 2)]),
    ('m-w-unwound-two-levels', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h2()) { leak2(); work() }\nfn(h()) { leak(); h2() }\ni := 0\ntry { while (i < 1) { i = i + 1; h() } } catch(e) { print(e.message) }\n', 'legacy', [('p', 2)]),
    ('m-w-cond-unwound-callee', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { leak(); work(); return 1 }\ntry { while (leak2() + h() > 0) { print("n") } } catch(e) { print(e.message) }\n', 'legacy', [('q', 2)]),
    ('m-w-fn-loop-in-callee', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { i := 0; while (i < 1) { i = i + 1; x := leak() + work() }; return 1 }\ni := 0\ntry { while (i < 1) { i = i + 1; y := h() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-u-try-in-body', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; x := leak() + work(); try { print("t") } catch(e) { print("c") } } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-u-try-in-body-cond', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { while (leak() + work() > 0) { try { print("t") } catch(e) { print("c") } } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-u-throw-in-body', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; x := leak() + work(); if (false) { throw error("never") } } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-u-throw-in-body-cond', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { while (leak() + work() > 0) { throw error("never") } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-u-throw-nested-inner', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; x := leak() + work(); j := 0; while (j < 0) { throw error("n") } } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-u-outer-prep-inner', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; try { print("t") } catch(e) { }; j := 0; while (j < 1) { j = j + 1; x := leak() + work() } } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-u-bare-return-fn', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { i := 0; while (i < 1) { i = i + 1; x := leak() + work(); if (false) { return } }; return }\ntry { h() } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-u-try-in-loop-stmt', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\nwhile (i < 1) { i = i + 1; try { x := leak() + work() } catch(e) { print(e.message) } }\n', 'correction', [('q', 1)]),
    ('m-u-try-in-loop-earlier', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\nwhile (i < 1) { i = i + 1; try { leak(); work() } catch(e) { print(e.message) } }\n', 'legacy', [('q', 1)]),
    ('m-f-body-decl', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { for (v : [1]) { x := leak() + work() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-f-iterable', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(arr()) { leak(); return [work()] }\ntry { for (v : arr()) { print(v) } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-f-iterable-same', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { for (v : [leak(), work()]) { print(v) } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-f-throw-in-body', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { for (v : [1]) { x := leak() + work(); if (false) { throw error("n") } } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-f-try-in-body', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { for (v : [1]) { x := leak() + work(); try { print("t") } catch(e) { } } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-f-unwound-frame', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(w()) { f := file("p"); f.open("r"); throw error("stop") }\ntry { for (v : [1]) { w() } } catch(e) { print(e.message) }\n', 'legacy', []),
    ('m-fn-handler-while-cond', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { try { while (leak() + work() > 0) { print("n") } } catch(e) { print(e.message) }; return 1 }\nprint(h())\n', 'legacy', [('q', 1)]),
    ('m-fn-handler-loop-decl', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { i := 0; try { while (i < 1) { i = i + 1; x := leak() + work() } } catch(e) { print(e.message) }; return 1 }\nprint(h())\n', 'legacy', [('q', 1)]),
    ('m-fn-loop-called-in-try', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { i := 0; while (i < 1) { i = i + 1; x := leak() + work() }; return 1 }\ntry { y := h() } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-cb-map-throw', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(cb(x)) { leak(); return work() }\ntry { y := [1].map(cb) } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-cb-map-in-loop', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(cb(x)) { leak(); return work() }\ni := 0\ntry { while (i < 1) { i = i + 1; y := [1].map(cb) } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-cb-map-in-loop-completed', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(cb(x)) { leak(); return x }\ni := 0\ntry { while (i < 1) { i = i + 1; y := [1].map(cb); work() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-catch-rethrow', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { try { while (i < 1) { i = i + 1; x := leak() + work() } } catch(e) { throw error("b") } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-loop-then-stmt', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; leak() }; x := leak2() + work() } catch(e) { print(e.message) }\n', 'correction', [('p', 2)]),
    ('m-w-two-iter', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 2) { i = i + 1; if (i == 2) { x := leak() + work() } else { y := leak2() } } } catch(e) { print(e.message) }\n', 'legacy', [('q', 2)]),
    ('m-top-two-leaks', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nleak()\nleak2()\n', 'legacy', [('p', 2)]),
    ('m-top-same-file-twice', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nleak()\nleak()\n', 'legacy', [('q', 2)]),
    ('m-w-prep-callee-local', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { f := file("p"); f.open("r"); work() }\ni := 0\ntry { while (i < 1) { i = i + 1; h() } } catch(e) { print(e.message) }\n', 'correction', []),
    ('m-w-prep-callee-local-decl', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { f := file("p"); f.open("r"); x := work() }\ni := 0\ntry { while (i < 1) { i = i + 1; h() } } catch(e) { print(e.message) }\n', 'correction', []),
    ('m-w-unprep-callee-completed', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { leak(); work(); if (false) { throw error("x") } }\ni := 0\ntry { while (i < 1) { i = i + 1; h() } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-w-unprep-callee-try', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { leak(); try { y := 1 } catch(e) { }; work() }\ni := 0\ntry { while (i < 1) { i = i + 1; h() } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-w-prep-callee-calls-unprep', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h2()) { leak2(); work(); if (false) { throw error("x") } }\nfn(h()) { leak(); h2() }\ni := 0\ntry { while (i < 1) { i = i + 1; h() } } catch(e) { print(e.message) }\n', 'correction', [('p', 2)]),
    ('m-w-unprep-calls-prep', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h2()) { leak2(); work() }\nfn(h()) { leak(); h2(); if (false) { throw error("x") } }\ni := 0\ntry { while (i < 1) { i = i + 1; h() } } catch(e) { print(e.message) }\n', 'correction', [('p', 2)]),
    ('m-w-unprep-stmt-in-callee', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h2()) { x := leak2() + work(); if (false) { throw error("x") } }\nfn(h()) { leak(); h2() }\ni := 0\ntry { while (i < 1) { i = i + 1; h() } } catch(e) { print(e.message) }\n', 'correction', [('p', 2)]),
    ('m-t-prep-fn-from-textual', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { leak(); work() }\ntry { h() } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-t-fn-loop-handler-out', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { i := 0; while (i < 1) { i = i + 1; leak(); work() } }\nfn(k()) { try { h() } catch(e) { print(e.message) }; return 0 }\ni := 0\nwhile (i < 1) { i = i + 1; print(k()) }\n', 'correction', [('q', 1)]),
    ('m-w-callee-decl-in-loop', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { x := leak() + work() }\ni := 0\ntry { while (i < 1) { i = i + 1; h() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-callee-loop-throw-decl', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { j := 0; while (j < 1) { j = j + 1; x := leak() + work() } }\ni := 0\ntry { while (i < 1) { i = i + 1; h() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-lambda-call', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nlam := () => leak() + work()\ni := 0\ntry { while (i < 1) { i = i + 1; x := lam() } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-w-lambda-call-completed', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nlam := () => leak()\ni := 0\ntry { while (i < 1) { i = i + 1; x := lam() + work() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-lambda-body-fn', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { leak(); work() }\nlam := () => h()\ni := 0\ntry { while (i < 1) { i = i + 1; x := lam() } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-w-struct-method', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nstruct(S) { fn(m()) { leak(); work() } }\ns := S()\ni := 0\ntry { while (i < 1) { i = i + 1; s.m() } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-w-cb-prep-fn', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(cb(x)) { leak(); return work() }\ni := 0\ntry { while (i < 1) { i = i + 1; y := [1].map(cb) } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-w-cb-calls-prep', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { leak(); return work() }\nfn(cb(x)) { return h() }\ni := 0\ntry { while (i < 1) { i = i + 1; y := [1].map(cb) } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-w-cb-decl-in-cb', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(cb(x)) { y := leak() + work(); return 1 }\ni := 0\ntry { while (i < 1) { i = i + 1; y := [1].map(cb) } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-w-uncaught', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\nwhile (i < 1) { i = i + 1; x := leak() + work() }\n', 'legacy', []),
    ('m-w-uncaught-earlier', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\nwhile (i < 1) { i = i + 1; leak(); work() }\n', 'legacy', []),
    ('m-f-nested-in-prep', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; for (v : [leak(), work()]) { print(v) } } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-f-iterable-dyn', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { for (v : [leak(), work()]) { print(v) } }\ni := 0\ntry { while (i < 1) { i = i + 1; h() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-f-iterable-prep-loop', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { for (v : [leak(), work()]) { x := 1 } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-w-in-unprep-fn-loop', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { i := 0; while (i < 1) { i = i + 1; x := leak() + work() }; if (false) { throw error("n") } }\ni := 0\ntry { while (i < 1) { i = i + 1; h() } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-w-textual-leaf', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; x := leak() + work() ?? 1 } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-catch-body-loop', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { throw error("a") } catch(e) { i := 0; try { while (i < 1) { i = i + 1; x := leak() + work() } } catch(e2) { print(e2.message) } }\n', 'legacy', [('q', 1)]),
    ('m-w-ret-in-prep-fn', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { x := leak() + work(); return x }\ni := 0\ntry { while (i < 1) { i = i + 1; h() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-ret-expr-prep-fn', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { return leak() + work() }\ni := 0\ntry { while (i < 1) { i = i + 1; h() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-struct-method-prep', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nstruct(S) { z := 1\n fn(m()) { x := leak() + work(); return 1 } }\ns := S()\ni := 0\ntry { while (i < 1) { i = i + 1; s.m() } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-w-struct-method-completed', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nstruct(S) { z := 1\n fn(m()) { leak(); return 1 } }\ns := S()\ni := 0\ntry { while (i < 1) { i = i + 1; y := s.m() + work() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-leaf-objlit', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; o := {"a": "s", "b": leak() + work()} } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-w-leaf-paren-coalesce', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; x := (leak() ?? 1) + work() } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-w-prep-callee-leaf-then-stmt', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { o := {"a": "s"}; x := leak() + work() }\ni := 0\ntry { while (i < 1) { i = i + 1; h() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('m-w-prep-chain-local', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h2()) { f := file("p"); f.open("r"); work() }\nfn(h()) { h2() }\ni := 0\ntry { while (i < 1) { i = i + 1; h() } } catch(e) { print(e.message) }\n', 'correction', []),
    ('m-w-unprep-leaf-order', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { leak(); o := {"a": "s"}; work(); if (false) { throw error("x") } }\ni := 0\ntry { while (i < 1) { i = i + 1; h() } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-w-nested-for-bad-iter', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; x := leak() + work(); for (v : ([1] ?? [2])) { print(v) } } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-w-fnvalue-call', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { x := leak() + work() }\ng := h\ni := 0\ntry { while (i < 1) { i = i + 1; g() } } catch(e) { print(e.message) }\n', 'correction', [('q', 1)]),
    ('m-w-prep-leaf-in-loop-then-stmt', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(leak2()) { g := file("p"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; o := {"a": "s"}; x := leak() + work() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('rev-while-cond-same-eval', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { while (leak() + work() > 0) { print("n") } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('rev-while-cond-and', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { while (leak() > 0 && work() > 0) { print("n") } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('rev-while-body-same-stmt', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; x := leak() + work() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('rev-while-body-if-cond-same', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; if (leak() + work() > 0) { print("n") } } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('rev-for-body-same-stmt2', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { for (v : [1]) { x := leak() + work() } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('rev-for-body-if-cond2', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ntry { for (v : [1]) { if (leak() + work() > 0) { print("n") } } } catch(e) { print(e.message) }\n', 'legacy', [('q', 1)]),
    ('rev-try-in-while-body-while-cond', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\ni := 0\nwhile (i < 1) { i = i + 1; try { while (leak() + work() > 0) { print("n") } } catch(e) { print(e.message) } }\n', 'legacy', [('q', 1)]),
    ('rev-while-in-try-in-while', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nj := 0\nwhile (j < 1) { j = j + 1; try { i := 0; while (i < 1) { i = i + 1; x := leak() + work() } } catch(e) { print(e.message) } }\n', 'legacy', [('q', 1)]),
    ('rev-fn-try-while-cond', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { try { while (leak() + work() > 0) { print("n") } } catch(e) { print(e.message) }; return 1 }\nprint(h())\n', 'legacy', [('q', 1)]),
    ('rev-while-cond-uncaught-in-fn', 'fn(leak()) { g := file("q"); g.open("r"); return 1 }\nfn(work()) { throw error("stop") }\nfn(h()) { try { x := 1; while (leak() + work() > 0) { x = 2 } } catch(e) { print("c") }; return 0 }\nprint(h())\n', 'legacy', [('q', 1)]),
    ('rev-unwound-frame-in-while', 'fn(w()) { f := file("p"); f.open("r"); throw error("stop") }\ni := 0\ntry { while (i < 1) { i = i + 1; w() } } catch(e) { print(e.message) }\n', 'legacy', []),
    ('rev-unwound-frame-in-for', 'fn(w()) { f := file("p"); f.open("r"); throw error("stop") }\ntry { for (v : [1]) { w() } } catch(e) { print(e.message) }\n', 'legacy', []),
    ('rev-unwound-frame-in-while-cond', 'fn(w()) { f := file("p"); f.open("r"); throw error("stop") }\ntry { while (w() > 0) { print("n") } } catch(e) { print(e.message) }\n', 'legacy', []),
    ('c-block-completed-then-throw', 'try { if (true) { f := file("p"); f.open("r") }; throw error("x") } catch(e) { print(e.message) }\n', 'legacy', [('p', 1)]),
    ('c-loop-iteration-completed', 'i := 0\ntry { while (i < 2) { i = i + 1; f := file("p"); f.open("r"); if (i == 2) { throw error("x") } } } catch(e) { print(e.message) }\n', 'correction', [('p', 1)]),
    ('c-for-iteration-completed', 'try { for (v : [1, 2]) { f := file("p"); f.open("r"); if (v == 2) { throw error("x") } } } catch(e) { print(e.message) }\n', 'correction', [('p', 1)]),
    ('c-continue-iterations', 'i := 0\ntry { while (i < 3) { i = i + 1; f := file("p"); f.open("r"); if (i < 3) { continue }; throw error("x") } } catch(e) { print(e.message) }\n', 'correction', [('p', 2)]),
    ('c-break-then-throw', 'i := 0\ntry { while (i < 3) { i = i + 1; f := file("p"); f.open("r"); break }; throw error("x") } catch(e) { print(e.message) }\n', 'legacy', [('p', 1)]),
    ('c-completed-inner-try-body', 'try { try { f := file("p"); f.open("r") } catch(e) { }; throw error("x") } catch(e) { print(e.message) }\n', 'legacy', [('p', 1)]),
    ('c-completed-catch-body', 'try { try { throw error("a") } catch(e) { g := file("p"); g.open("r") }; throw error("x") } catch(e) { print(e.message) }\n', 'legacy', [('p', 1)]),
    ('c-catch-body-local-rethrow', 'try { try { throw error("a") } catch(e) { g := file("p"); g.open("r"); throw error("b") } } catch(e) { print(e.message) }\n', 'correction', []),
    ('c-alias-survives', 'f := file("p")\nf.open("r")\ntry { g := f; throw error("x") } catch(e) { print(e.message) }\nprint(f.read_line())\nf.close()\n', 'legacy', []),
    ('c-unrelated-survives', 'f := file("p")\nf.open("r")\ntry { throw error("x") } catch(e) { print(e.message) }\nprint(f.read_line())\nf.close()\n', 'legacy', []),
    ('c-outer-opened-in-try', 'f := file("p")\ntry { f.open("r"); throw error("x") } catch(e) { print(e.message) }\nprint(f.read_line())\nf.close()\n', 'legacy', []),
    ('c-outer-open-left', 'f := file("p")\ntry { f.open("r"); throw error("x") } catch(e) { print(e.message) }\n', 'legacy', [('p', 1)]),
    ('c-try-body-unsaved-write', 'try { f := file("w.txt"); f.open("w"); f.write("data"); throw error("x") } catch(e) { print(e.message) }\nprint(file("w.txt").exists())\n', 'correction', []),
    ('c-else-completed-then-throw', 'try { if (false) { print("n") } else { f := file("p"); f.open("r") }; throw error("x") } catch(e) { print(e.message) }\n', 'legacy', [('p', 1)]),
    ('c-uncaught-in-loop', 'i := 0\nwhile (i < 1) { i = i + 1; f := file("p"); f.open("r"); throw error("x") }\n', 'legacy', []),
    ('c-two-open-unwound', 'fn(w()) { f := file("p"); f.open("r"); g := file("q"); g.open("r"); throw error("x") }\ntry { w() } catch(e) { print(e.message) }\n', 'legacy', []),
]


def run(nift, root, source, vm):
    (root / "case.f").write_text(source)
    for name in ("w.txt", "out.txt", "stats.jsonl"):
        if (root / name).exists():
            (root / name).unlink()
    env = dict(os.environ)
    env["NIFT_VM"] = "require" if vm else "0"
    env.pop("NIFT_VM_STATS", None)
    env["NIFT_HANDLE_LIMIT"] = "8"  # repeated cases: unwound and abandoned files must all be reclaimed
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
    """(file name, file count) of each left-open report (legacy and the VM word it the same way)."""
    return [(Path(path).name, int(count) if count else 1)
            for count, path in re.findall(r"managed file left open(?: \((\d+) files\))?: (\S+)", err)]


def check(mode, expected, legacy, vm):
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
    expected = [tuple(e) for e in expected]
    if reported(ve) != expected:
        problems.append(f"VM reports {reported(ve)}, contract {expected}")
    if expected and vrc != 1:
        problems.append(f"VM exit {vrc} with reports {reported(ve)}")
    if mode == "correction" and not expected and vrc != 0:
        problems.append(f"VM exit {vrc} where the contract releases silently: {ve!r}")
    if lw != vw:
        problems.append(f"written files differ: legacy {lw!r} vm {vw!r}")
    if lo != vo:
        problems.append(f"stdout differs: legacy {lo!r} vm {vo!r}")
    if mode == "legacy":
        if lrc != vrc or (le == "") != (ve == "") or reported(le) != reported(ve):
            problems.append(f"legacy differs (case says identical): legacy {(lrc, reported(le), le)!r} "
                            f"vm {(vrc, reported(ve))!r}")
    elif mode == "correction":
        if reported(le) == expected:
            problems.append(f"legacy now matches the contract {expected}: reclassify as `legacy`")
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    if not os.access(nift, os.X_OK):
        print(f"FAIL: no executable {nift}")
        return 1
    failures = corrections = 0
    for name, source, mode, expected in CASES:
        with tempfile.TemporaryDirectory(prefix="cp17-unwind-") as tmp:
            root = Path(tmp)
            (root / "p").write_text("one\ntwo\n")
            (root / "q").write_text("q\n")
            legacy = run(nift, root, source, False)
            vm = run(nift, root, source, True)
            problems = check(mode, expected, legacy, vm)
            if problems:
                failures += 1
                print(f"FAIL {name} ({mode}): " + "; ".join(problems))
            else:
                corrections += mode == "correction"
                print(f"ok   {name} ({mode})")
    print(f"{'PASS' if not failures else 'FAIL'} cp17 unwind release: {len(CASES)} cases, {failures} failure(s); "
          f"{corrections} approved correction(s) against legacy")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
