i := 0
while (i < 1000000) {
  try {
    throw error("x")
  } catch(e) {
    i += 1
  }
}
print("ok")
