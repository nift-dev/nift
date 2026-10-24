try {
  h := sha256_file("missing-file")
} catch(e) {
  print("caught " + e.code + " " + e.line.to_string() + ":" + e.column.to_string())
}
