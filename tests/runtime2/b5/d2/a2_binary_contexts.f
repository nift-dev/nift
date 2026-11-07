// D2 (docs/runtime-2/checkpoints/combined-b3-b5.md section 27; owner rulings 2026-11-08 03:27/03:29) family A in contexts; legacy-equal (all 10)
// expected: 10 (top) 10 (loop) 10 (fn global) 10 (lambda global) 10 (fn cell) 10 (fn local, inline assignment)
x := 6
g := () => { x = 20; return 4 }
print(x + g())
x = 6
once := 0
while (once < 1) { once += 1; print(x + g()) }
x = 6
fn(t1()) { print(x + g()); return 0 }
t1()
x = 6
t2 := () => { print(x + g()); return 0 }
t2()
fn(t3()) { y := 6; h := () => { y = 20; return 4 }; print(y + h()); return 0 }
t3()
fn(t4()) { y := 6; print(y + ((y = 20) - 16)); return 0 }
t4()
