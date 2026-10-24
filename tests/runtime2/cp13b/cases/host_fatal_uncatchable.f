try {
  sleep("x")
  print("ok")
} catch(e) {
  print("caught " + e.code)
}
print("after")
