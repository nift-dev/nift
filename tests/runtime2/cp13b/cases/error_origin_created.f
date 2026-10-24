e := error("made")
print("[" + e.source + "] " + e.line.to_string() + ":" + e.column.to_string())
try {
  throw e
} catch(c) {
  print("[" + c.source + "] " + c.line.to_string() + ":" + c.column.to_string())
}
print(e.stringify())
