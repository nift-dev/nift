m := map(); m.set(1, 1)
fn(w()) { m.set(2, 2); return m.size() }
t := thread(w)
print(t.join())
print(m.size())
