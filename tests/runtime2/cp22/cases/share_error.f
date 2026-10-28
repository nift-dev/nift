fn[async](bad()) { sleep(2); throw error("shared boom", "user.shared") }
fn[async](catcher(p, k)) { r := 0; try { await p } catch(e) { r = k }; return r }
p := bad()
a := catcher(p, 1)
b := catcher(p, 2)
print(await a)
print(await b)
try { await p } catch(e1) { try { await p } catch(e2) { print(e1 == e2); print(e1.message); print(e1.code) } }
