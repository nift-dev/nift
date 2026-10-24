try {
  v := validate({"type": "string"}, 5)
  print("no")
} catch(e) {
  print(e.code)
  print(e.category)
  print(e.message)
  print(e.line.to_string() + ":" + e.column.to_string())
}
