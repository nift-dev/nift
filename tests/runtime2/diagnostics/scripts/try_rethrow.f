try {
  throw error("first")
} catch(e) {
  print("caught " + e.message)
  throw e
}
print("not reached")
