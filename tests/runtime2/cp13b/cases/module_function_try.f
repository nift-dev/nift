import("m.nft")
try {
  must_fail("entry")
} catch(e) {
  print("caught " + e.message)
  print("origin " + e.line.to_string() + ":" + e.column.to_string())
  print("source " + e.source)
}
