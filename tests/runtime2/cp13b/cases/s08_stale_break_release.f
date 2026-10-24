fn(f()) {
  i := 0
  while (i < 3) {
    i += 1
    try {
      if (i == 2) { break }
    } catch(a) {
      print("STALE inner handler")
    }
  }
  throw error("after break")
}
try { f() } catch(e) { print("caller " + e.message) }
j := 0
while (j < 3) {
  j += 1
  try {
    if (j == 2) { break }
  } catch(b) {
    print("STALE main handler")
  }
}
print("before uncaught")
throw error("must be uncaught")
