try {
  try {
    throw error("inner")
  } catch(a) {
    print("inner caught " + a.message)
    throw error("from inner catch")
  }
} catch(b) {
  print("outer caught " + b.message)
}
print("end")
