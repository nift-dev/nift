fn(boom(x)) { throw error("bad", "user.x") }
fn(kick(x)) { t := thread(boom, x); try { t.join() } catch(e) { return e.code }; return 0 }
k := kick
r := [1].map(k)
fn(result()) { return r[0] }
export(result)
