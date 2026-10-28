id := (n) => { return n }
fn[async](a(fs)) { for (print : fs) { sleep(1); print("from a") }; return 1 }
fn[async](b(fs)) { for (print : fs) { sleep(1); print("from b") }; return 2 }
x := a([id])
y := b([id])
print((await x) + (await y))
