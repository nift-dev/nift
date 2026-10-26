orig := error("passed", "user.p")
fn[async](rethrow(e)) { throw e }
try { await rethrow(orig) } catch(c) { print(c.message); print(c.code); print(c.line); print(c.column) }
