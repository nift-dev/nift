k := stack(); k.push(1); k.push(2); k.push(3)
for (x : k) { print(x) }
m := map(); m.set("a", 1); m.set(2, "b"); m.set(true, [1])
for ((kk, v) : m) { print(type(kk)); print(kk); print(type(v)) }
s := set(); s.add("z"); s.add(1)
for (x : s) { print(x) }
for (x : k) { k.push(9) }
print(k.size())
for ((a, b) : m) { m.set("new", 1) }
print(m.size())
for (x : s) { print(loop.index) }
fn(total(st)) { n := 0; for (x : st) { n += x }; return n }
print(total(k))
