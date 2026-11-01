print("ran")
k := stack()
k.push(map())
for (m : k) { m.set(1, 1) }
t := k.top()
print(t.size())
