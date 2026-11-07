// D2 (docs/runtime-2/checkpoints/combined-b3-b5.md section 27): APPROVED 2026-11-08 03:27/03:29 (owner), a difference, not legacy parity: legacy's prepared loop evaluator reads a compound target late when the callee is a `fn` (24, and `Za` for a string) and loses a lambda callee's string append (`Z`); the VM uses the canonical order in loops too (10, aa, aa).
x := 6
fn(g()) { x = 20; return 4 }
s := "a"
k := () => { s = "Z"; return "a" }
t := "a"
fn(kf()) { t = "Z"; return "a" }
once := 0
while (once < 1) { once += 1; x += g(); print(x); s += k(); print(s); t += kf(); print(t) }
for (y : [6]) { m := () => { y = 20; return 4 }; y *= m(); print(y) }
