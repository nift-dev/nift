fn[async](f(x)) { return "val" }
f(1)
print("a")
await f(2)
print("b")
