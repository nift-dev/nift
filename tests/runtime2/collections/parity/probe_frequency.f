m := map()
i := 0
while (i < 2000) { k := i % 37; v := 0; if (m.contains(k)) { v = m.get(k) }; m.set(k, v + 1); i += 1 }
print(m.size())
print(m.get(5))
s := set(); i = 0; while (i < 2000) { s.add(i); i += 1 }; i = 0; n := 0; while (i < 2000) { if (s.contains(i)) { n += 1 }; i += 1 }; print(n)
