// D2 (docs/runtime-2/checkpoints/combined-b3-b5.md section 27, r05): a pre-existing, recorded legacy difference (compatibility.md), no code change: in loop code legacy reads a call argument late when the called function is a lambda (legacy 6 20 6); the VM evaluates arguments once, left to right (6 6 6).
x := 6
g := () => { x = 20; return 4 }
fn(f2(a, c)) { return a }
l2 := (a, c) => { return a }
once := 0
while (once < 1) { once += 1; print(f2(x, g())); x = 6; print(l2(x, g())) }
x = 6
print(l2(x, g()))
