fn(recurse(n)) {
  try {
    return recurse(n + 1)
  } catch(e) {
    print("caught")
    return 0
  }
}
print("start")
recurse(0)
print("end")
