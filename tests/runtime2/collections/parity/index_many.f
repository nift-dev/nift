m := map()
i := 0
while (i < 300) { m.set(i, i * 2); i += 1 }
i = 0
while (i < 300) { if (i % 3 == 0) { m.remove(i) }; i += 1 }
print(m.size())
print(m.get(299))
print(m.contains(3.0))
print(m.contains(4.0))
s := set()
i = 0
while (i < 300) { s.add(i % 50); i += 1 }
print(s.size())
s.remove(10)
print(s.contains(10))
print(s.contains(11.0))
