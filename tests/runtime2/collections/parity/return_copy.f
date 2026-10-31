a := map()
fn(get_a()) { return a }
c := get_a()
c.set(5, 5)
print(a.size())
print(c.size())
fn(keep(m)) { return m }
d := keep(c)
d.set(6, 6)
print(c.size())
print(d.size())
