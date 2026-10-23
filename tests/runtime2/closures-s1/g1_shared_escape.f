fn(make()) {
  a := 1
  f := () => { a += 1
    return a }
  g := () => { h := () => { a += 10
    return a }
    return h() }
  return {"f": f, "g": g}
}
p := make()
print(p.f())
print(p.g())
print(p.f())
