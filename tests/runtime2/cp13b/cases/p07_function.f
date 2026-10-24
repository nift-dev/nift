fn(make()) {
  return error("from fn")
}
fn(raise(value)) {
  throw value
}
x := make()
print("x " + x.line.to_string() + ":" + x.column.to_string())
try {
  raise(x)
} catch(c) {
  print("caught " + c.line.to_string() + ":" + c.column.to_string())
  try {
    raise(c)
  } catch(d) {
    print("again " + d.line.to_string() + ":" + d.column.to_string())
  }
}
print("x after " + x.line.to_string() + ":" + x.column.to_string())
