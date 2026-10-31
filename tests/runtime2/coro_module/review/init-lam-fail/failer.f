fn(boom(x)) { throw error("bad", "user.x") }
k := (x) => thread(boom, x).join()
r := [1].map(k)
