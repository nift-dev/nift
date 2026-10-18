// fields of a caught user error
try {
  x := 1
  throw error("boom")
} catch(e) {
  print(e.message)
  print(e.code)
  print(e.category)
  print(e.line)
  print(e.column)
  print(e.stringify())
}
