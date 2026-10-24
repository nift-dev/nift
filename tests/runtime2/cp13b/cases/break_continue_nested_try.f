i := 0
while (i < 5) {
  try {
    try {
      i += 1
      if (i == 2) { continue }
      if (i == 4) { break }
    } catch(a) {
      print("inner caught")
    }
  } catch(b) {
    print("outer caught")
  }
  print("body " + i.to_string())
}
print("after " + i.to_string())
try {
  throw error("post")
} catch(e) {
  print("post " + e.message)
}
