try {
  try {
    throw error("first")
  } catch(e) {
    print("handling " + e.message)
    throw error("second")
  }
} catch(e) {
  print("outer " + e.message)
}
print("after")
