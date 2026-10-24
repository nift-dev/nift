fn(inner()) {
  return "ok"
}
fn(middle()) {
  try {
    return inner()
  } catch(e) {
    return "middle"
  }
}
fn(outer()) {
  try {
    return middle()
  } catch(e) {
    return "outer"
  }
}
print(outer())
fn(fail_now()) {
  throw error("second")
}
try {
  fail_now()
} catch(e) {
  print("main " + e.message)
}
print("end")
