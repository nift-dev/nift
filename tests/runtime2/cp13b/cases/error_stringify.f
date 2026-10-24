try {
  throw error("boom")
} catch(e) {
  print(e.stringify())
}
