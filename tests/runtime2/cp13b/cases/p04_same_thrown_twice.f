e := error("twice")
try {
  throw e
} catch(c) {
  print("1 " + c.line.to_string() + ":" + c.column.to_string())
}
try {
    throw e
} catch(c) {
  print("2 " + c.line.to_string() + ":" + c.column.to_string())
}
print("e " + e.line.to_string() + ":" + e.column.to_string())
