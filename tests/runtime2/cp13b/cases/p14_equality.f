e := error("x")
f := error("x")
print(e == f)
try { throw e } catch(c) {
  print(c == e)
  try { throw c } catch(d) { print(d == c) }
}
