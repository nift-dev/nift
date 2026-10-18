print("start")
try {
  x := 1
  undefined_fn(1)
} catch(e) {
  print("caught " + e.message)
}
print("end")
