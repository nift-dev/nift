fn(fill(m)) { m.set("f", 1); return m.size() }
a := map()
print(fill(a))
print(a.size())
fn(drain(k)) { k.pop(); return k.size() }
k := stack(); k.push(1); k.push(2)
print(drain(k))
print(k.size())
