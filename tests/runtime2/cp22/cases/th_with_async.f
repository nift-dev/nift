fn(add(a, b)) { return a + b }
fn[async](f(a)) { return a }
t := thread(add, 2, 3)
print(t.join())
print(await f(1))
