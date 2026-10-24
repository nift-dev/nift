i := 0
while (i < 5) {
  i += 1
  try { throw error("x") } catch(e) {
    if (i == 2) { continue }
    if (i == 3) { break }
  }
  print(i)
}
print("after " + i.to_string())
try { throw error("post") } catch(e) { print("post") }
