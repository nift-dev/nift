// D2 (docs/runtime-2/checkpoints/combined-b3-b5.md section 27; owner rulings 2026-11-08 03:27/03:29) family B: a compound assignment reads its target before the rhs, in every context; legacy-equal (10 10 10 aa 10)
x := 6
g := () => { x = 20; return 4 }
x += g()
print(x)
x = 6
fn(t1()) { x += g(); print(x); return 0 }
t1()
fn(t2()) { y := 6; h := () => { y = 20; return 4 }; y += h(); print(y); return 0 }
t2()
s := "a"
k := () => { s = "Z"; return "a" }
s += k()
print(s)
for (z : [6]) { m := () => { z = 20; return 4 }; z += m(); print(z) }
