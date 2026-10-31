a := map()
a.set("k", 1)
b := a
b.set("k", 2)
b.set("n", 3)
print(a.stringify())
print(b.stringify())
s := set(); s.add(1)
t := s
t.add(2)
print(s.stringify())
k := stack(); k.push(1)
j := k
j.pop()
print(k.stringify())
print(j.stringify())
