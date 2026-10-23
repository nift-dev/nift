struct(P) { a := 1
  fn(get()) { return () => { return [1] } } }
p := P()
h := p.get()
v := h()
print(v)
