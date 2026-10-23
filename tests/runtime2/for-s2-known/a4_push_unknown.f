fn(mk()) { return () => { return 5 } }
h := mk()
v := h()
v.push(1)
print(v)
