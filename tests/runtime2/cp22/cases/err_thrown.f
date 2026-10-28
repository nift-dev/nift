fn[async](bad()) { throw error("boom") }
p := bad()
try { x := await p } catch(e) { print(e.message); print(e.code); print(e.line); print(e.column) }
x := await p
