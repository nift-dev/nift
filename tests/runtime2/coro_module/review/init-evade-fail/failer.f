fn(boom(x)) { throw error("bad", "user.x") }
fn(kick(x)) { t := thread(boom, x); t.join(); return 0 }
k := kick
r := [1].map(k)
