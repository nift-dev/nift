try {
  h := sha256_file(".")
  print("no")
} catch(e) {
  print("caught " + e.code)
}
print("after")
