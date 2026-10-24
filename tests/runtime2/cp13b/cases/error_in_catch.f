try {
  throw error("first")
} catch(e) {
  print("handling")
  print(nosuch)
}
print("not reached")
