#!/usr/bin/env python3
# CP43 fuzz/property test: for each generated program, run the prepared-AST
# variant and a forced-legacy twin (a bare binding expression statement in the
# loop body disables prepared execution) and require identical stdout/exit.
# Also checks per-run determinism. Bounded and self-contained (no external
# reference binary); the generator covers arithmetic, compound assignment,
# declarations, native array calls, and nested if/break/continue.
import random
import subprocess
import sys
import os

random.seed(int(os.environ.get("V44_FUZZ_SEED", "1337")))
NIFT = os.environ.get("NIFT_BIN") or os.path.join(os.path.dirname(__file__), "..", "nift")
NIFT = os.path.abspath(NIFT)
TMP = "/tmp"
seed_counter = 0

def expr(depth=0):
    r = random.random()
    if depth > 2 or r < 0.3:
        return random.choice(["i", "total", "x", "n"])
    if r < 0.45:
        return "({} {} {})".format(expr(depth + 1), random.choice(["+", "-", "*"]), random.choice(["1", "2", "3"]))
    if r < 0.6:
        return "({} % {})".format(expr(depth + 1), random.choice(["2", "3", "7"]))
    if r < 0.7:
        return "({} {} {})".format(expr(depth + 1), random.choice(["<", ">", "=="]), random.choice(["0", "5"]))
    return "(i % {})".format(random.choice(["2", "3"]))

def body_stmt():
    r = random.random()
    if r < 0.35:
        return "total += {}".format(expr())
    if r < 0.5:
        return "i += 1"
    if r < 0.6:
        return "if(i % 2 == 0) { continue }"
    if r < 0.7:
        return "x := {}; total += x".format(expr())
    if r < 0.8:
        return "if(i > " + random.choice(["3", "5", "8"]) + ") { break }"
    return "a.push(i)"

def make_pair():
    n = random.choice([5, 20, 100])
    # Always increment i first so the loop terminates regardless of the other
    # generated statements. Both variants share the SAME body; the legacy twin
    # appends a bare binding expression that disables prepared execution.
    body = "i += 1; " + "; ".join(body_stmt() for _ in range(random.randint(1, 3)))
    header = "\n".join(["n := 0", "total := 0", "i := 0", "a := []"])
    prints = "\n".join(["print(total)", "print(a.size())", "print(i)"])
    prog = header + "\nwhile(i < " + str(n) + ") { " + body + " }\n" + prints + "\n"
    prog_legacy = header + "\nwhile(i < " + str(n) + ") { " + body + "; n }\n" + prints + "\n"
    return prog, prog_legacy

def run(prog):
    global seed_counter
    seed_counter += 1
    path = os.path.join(TMP, "v44_fuzz_{}.f".format(seed_counter))
    with open(path, "w") as f:
        f.write(prog)
    try:
        r = subprocess.run([NIFT, "run", path], capture_output=True, text=True, timeout=20)
    except subprocess.TimeoutExpired:
        return None, None
    finally:
        try:
            os.remove(path)
        except OSError:
            pass
    return r.returncode, r.stdout

fails = 0
runs = 0
for _ in range(int(os.environ.get("V44_FUZZ_ITERS", "120"))):
    prog, prog_legacy = make_pair()
    a1 = run(prog)
    a2 = run(prog)
    b = run(prog_legacy)
    if a1 is None or a2 is None or b is None:
        continue
    runs += 1
    if a1 != a2:
        print("NONDETERMINISTIC:\n" + prog)
        fails += 1
        if fails >= 3:
            break
    if a1 != b:
        print("AST/LEGACY DIVERGENCE:\n" + prog + "\n--- prepared:", a1, "\n--- legacy:", b)
        fails += 1
        if fails >= 3:
            break

if fails:
    print("v4.4 AST fuzz: FAIL ({} runs, {} failures)".format(runs, fails), file=sys.stderr)
    sys.exit(1)
print("v4.4 AST fuzz: PASS ({} runs, prepared == legacy, deterministic)".format(runs))