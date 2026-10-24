fn(make(unused)) {
  try {
    throw error("escaped")
  } catch(e) {
    return () => e.message
  }
}
f := make(0)
print(f())
