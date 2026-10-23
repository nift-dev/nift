fn(mk()) { return () => { return [1] } }
h := mk()
v := h()
print(v)
