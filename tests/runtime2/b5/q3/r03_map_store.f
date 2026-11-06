// Q3 row 3 (§3 Element store): the map holds the stored array itself.
a := [1]
m := map()
m.set("k", a)
a.push(2)
print(m.get("k").stringify())
