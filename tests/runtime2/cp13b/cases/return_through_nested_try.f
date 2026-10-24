fn(f()) {
  try {
    try {
      return "value"
    } catch(a) {
      return "inner"
    }
  } catch(b) {
    return "outer"
  }
}
print(f())
try {
  throw error("later")
} catch(e) {
  print("later " + e.message)
}
