fn(id(x)) { return x }
fn(joiner(h)) { return h.join() }
fn[async](a(x)) { return x + 1 }
fn(waiter(p)) { return await p }
fn[async](b(t)) { return t.join() * 10 }
t := thread(id, 7)
j := thread(joiner, t)
print(j.join())
print(t.join())
p := a(1)
w := thread(waiter, p)
print(w.join())
print(await b(t))
fn(reads()) { return t.join() + 1 }
u := thread(reads)
print(u.join())
