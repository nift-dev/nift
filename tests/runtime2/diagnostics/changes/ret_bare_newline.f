fn(f()) {
  print("before")
  return
  print("never")
}
f()
print("after")
