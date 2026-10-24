fn(f()) {
  return 1
}
try {
  x := 1;   throw    error("spaced")
} catch(c) {
  print(c.line.to_string() + ":" + c.column.to_string())
}
