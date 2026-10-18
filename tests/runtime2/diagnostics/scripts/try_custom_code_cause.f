inner := error("inner reason")
try {
  throw error("outer reason", "user.app.failed", inner)
} catch(e) {
  print(e.code)
  print(e.category)
  print(e.cause.message)
  print(e.cause.code)
}
