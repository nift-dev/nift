try {
  print("printed inside try before throw")
  throw error("x")
} catch(e) {
  print("caught")
}
fn(f()) {
  print("printed in callee before throw")
  throw error("y")
}
try { f() } catch(e) { print("caught2") }
