@fn[async](sq(n)) { return n * n }
x := sq(3)
print("out-1")
warn("err-1")
print(await x)
