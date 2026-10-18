try {
  @json(v, "lib/bad.json")
} catch(e) {
  print(e.code)
  print(e.category)
}
