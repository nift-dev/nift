import("r.f")
fn[async](go(n)) { return fib(n) }
t := thread(fib, 15)
print(t.join())
print(await go(12))
