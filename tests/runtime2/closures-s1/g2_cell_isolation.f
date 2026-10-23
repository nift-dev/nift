fn(make(v)) { return () => v }
a := make(10)
b := make(100)
print(a())
print(b())
print(a())
