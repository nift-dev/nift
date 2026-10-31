fn(chk(m, n)) { return same(m, n) }
fn(two(m)) { return m }
print("ran")
a := mutex(0)
c := mutex(0)
t := thread(chk, a, a)
print(t.join())
u := thread(chk, a, c)
print(u.join())
r := thread(two, a)
back := r.join()
print(same(back, a))
