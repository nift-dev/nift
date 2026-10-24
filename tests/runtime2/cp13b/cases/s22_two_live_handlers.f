try {
  try {
    throw error("inner")
  } catch(a) {
    print("inner " + a.message)
  }
  throw error("outer")
} catch(b) {
  print("outer " + b.message)
}
print("end")
