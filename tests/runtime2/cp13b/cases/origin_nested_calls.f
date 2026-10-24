fn(level3()) {
  throw error("deep")
}
fn(level2()) {
  level3()
}
fn(level1()) {
  level2()
}
try {
  level1()
} catch(e) {
  print(e.message)
  print(e.line.to_string() + ":" + e.column.to_string())
  print(e.source)
}
