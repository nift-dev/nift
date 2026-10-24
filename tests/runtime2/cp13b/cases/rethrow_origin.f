fn(f()) {
  try {
    throw error("outer")
  } catch(e) {
    throw e
  }
}
try {
  f()
} catch(e) {
  print(e.message)
  print(e.line.to_string() + ":" + e.column.to_string())
}
