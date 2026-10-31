mp := map(); mp.set(1, "a"); mp.set(2, "b")
for ((k, v) : mp) { mp.set(k + 10, v); print(k); print(v) }
print(mp.stringify())
s := set(); s.add(1); s.add(2)
for (x : s) { s.add(x + 10); print(x) }
print(s.stringify())
