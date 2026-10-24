root := error("root")
try {
  throw root
} catch(r) {
  mid := error("mid", "user.app.mid", r)
  try {
    throw mid
  } catch(m) {
    print("m " + m.line.to_string() + ":" + m.column.to_string())
    print("m.cause " + m.cause.line.to_string() + ":" + m.cause.column.to_string() + " " + m.cause.message)
    top := error("top", "user.app.top", m)
    print(top.stringify())
    try { throw top } catch(t) {
      print("t " + t.line.to_string() + ":" + t.column.to_string())
      print("t.cause " + t.cause.line.to_string() + ":" + t.cause.column.to_string())
      print("t.cause.cause " + t.cause.cause.line.to_string() + ":" + t.cause.cause.column.to_string())
    }
  }
}
uncause := error("u")
w := error("w", "user.app.w", uncause)
try { throw w } catch(c) {
  print("w " + c.line.to_string() + ":" + c.column.to_string() + " cause " + c.cause.line.to_string() + ":" + c.cause.column.to_string())
}
