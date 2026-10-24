try {
  throw error("m", "bad code")
} catch(e) {
  print("caught")
}
print("after")
