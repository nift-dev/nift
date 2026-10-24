cause := error("the cause")
fn(reraise()) {
  try {
    throw error("outer", "user.app.failed", cause)
  } catch(e) {
    print("first " + e.line.to_string() + ":" + e.column.to_string())
    throw e
  }
}
try {
  reraise()
} catch(e) {
  print("second " + e.line.to_string() + ":" + e.column.to_string())
  print(e.message + " / " + e.code + " / " + e.cause.message)
}
