id := (n) => { return n }
fn[async](a(fs)) { sleep(100); for (print : fs) { sleep(1); print("a") }; return 1 }
fn[async](b(fs)) { sleep(150); for (print : fs) { sleep(1); print("b") }; return 2 }
fn[async](c(fs)) { for (print : fs) { sleep(1); print("c") }; return 3 }
x := a([id])
y := b([id])
z := c([id])
print((await x) + (await y) + (await z))
