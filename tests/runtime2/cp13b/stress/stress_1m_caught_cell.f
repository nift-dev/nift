i := 0
while (i < 1000000) {
  try {
    throw error("x")
  } catch(e) {
    f := () => e.message
    if (f() != "x") { print("bad") }
    i += 1
  }
}
print("ok")
