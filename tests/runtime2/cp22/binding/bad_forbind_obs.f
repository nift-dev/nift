id := (n) => { return n }
fn[async](a(fs)) { i := 0; for (sleep : fs) { sleep(1); while (i < 3) { sleep(200); i += 1 } }; return "a" }
fn[async](b()) { sleep(100); return "b" }
x := a([id])
y := b()
print(await x)
print(await y)
