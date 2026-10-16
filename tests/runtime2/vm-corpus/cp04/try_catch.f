fn(bad()) { throw error("nope") }
try {
  bad()
} catch(e) {
  print("caught", e)
}
x := 1
try {
  x = undefined_thing(2)
} catch(e2) {
  print("caught2")
}
print(x)
