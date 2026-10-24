try {
  text := cat("/nonexistent/cp13b-host")
  print(text)
} catch(e) {
  print(e.code)
  print(e.message)
  print(e.line.to_string() + ":" + e.column.to_string())
}
