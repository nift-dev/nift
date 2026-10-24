e := error("via closure")
thrower := () => { throw e }
try {
  thrower()
} catch(c) {
  print("caught " + c.line.to_string() + ":" + c.column.to_string())
}
print("e " + e.line.to_string() + ":" + e.column.to_string())
maker := () => error("made in closure")
m := maker()
print("m " + m.line.to_string() + ":" + m.column.to_string())
try {
  throw m
} catch(c) {
  print("m thrown " + c.line.to_string() + ":" + c.column.to_string())
}
