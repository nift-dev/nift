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
bad := make_bad()
h := make_catcher()
print(h())
