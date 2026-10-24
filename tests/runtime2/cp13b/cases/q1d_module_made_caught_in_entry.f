import("lib.nft")
try { make_and_throw("m") } catch(c) {
  print(c.source)
  print(c.line.to_string() + ":" + c.column.to_string())
  print(c.stringify())
}
