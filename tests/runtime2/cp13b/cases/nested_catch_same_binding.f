try {
  throw error("outer")
} catch(e) {
  try {
    throw error("inner")
  } catch(e) {
    print("inner " + e.message)
  }
  print("outer again " + e.message)
}
