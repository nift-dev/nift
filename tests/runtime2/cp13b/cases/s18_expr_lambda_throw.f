fn(boom()) {
  throw error("boom")
}
g := () => boom()
try {
  g()
} catch(e) {
  print("caught [" + e.message + "] " + e.line.to_string() + ":" + e.column.to_string())
}
h := () => {
  return g()
}
try { h() } catch(e) { print("caught2 [" + e.message + "]") }
g()
