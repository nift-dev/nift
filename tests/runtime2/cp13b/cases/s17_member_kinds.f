e := error("m")
print(e.line + 1)
print(e.cause == null)
x := e.code + "!"
print(x)
