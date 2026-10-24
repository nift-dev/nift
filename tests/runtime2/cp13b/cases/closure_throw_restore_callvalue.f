fn(make_bad()) {
  tag := "WRONG"
  return () => { t := tag; throw error("boom") }
}
fn(make_catcher()) {
  msg := "ok"
  return () => {
    try {
      bad()
    } catch(e) {
      print("caught " + e.message)
    }
    return "after " + msg
  }
}
fn(callit(fnarg)) {
  return fnarg()
}
bad := make_bad()
h := make_catcher()
print(callit(h))
