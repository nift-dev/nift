fn[async](bad()) { throw error("boom", "user.x") }
p := bad()
try { await p } catch(e) {
  try { await p } catch(e2) { print(e2 == e); print(e2.code); print(e2.line); print(e2.column) }
}
