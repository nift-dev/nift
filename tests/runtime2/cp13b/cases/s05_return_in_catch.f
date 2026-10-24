fn(f()) {
  try { throw error("x") } catch(e) {
    return "from catch " + e.message
  }
  return "end"
}
print(f())
try { throw error("later") } catch(e) { print("later ok") }
