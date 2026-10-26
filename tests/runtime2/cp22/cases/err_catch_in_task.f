fn[async](bad()) { throw error("inner", "user.x") }
fn[async](mid()) { try { return await bad() } catch(e) { return "caught " + e.code } }
print(await mid())
