mk := (m) => error(m)
e := mk("from lambda")
print(e.message + " " + e.line.to_string())
k := () => mk("x").message
print(k())
