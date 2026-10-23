fn(mk()) { return () => { return () => 1 } }
h := mk()
s := "abc"
s = h()
print(s)
