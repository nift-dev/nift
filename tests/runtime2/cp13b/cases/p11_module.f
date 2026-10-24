import("m.nft")
a := mk("made in module")
print("a [" + a.source + "] " + a.line.to_string() + ":" + a.column.to_string())
try { boom("boom in module") } catch(c) {
  print("boom [" + c.source + "] " + c.line.to_string() + ":" + c.column.to_string())
}
try { rethrow(a) } catch(c) {
  print("rethrow-unoriginated [" + c.source + "] " + c.line.to_string() + ":" + c.column.to_string())
}
e := error("entry")
try { throw e } catch(c) {
  try { rethrow(c) } catch(d) {
    print("rethrow-originated [" + d.source + "] " + d.line.to_string() + ":" + d.column.to_string())
  }
}
