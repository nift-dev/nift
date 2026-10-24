fn(fail(m)) {
  x := 1
  throw error(m)
}
try {
  fail("from function")
} catch(e) {
  print(e.message)
  print(e.line)
  print(e.column)
}
