mp := map(); mp.set(1, "a"); mp.set(2, "b")
for ((k, v) : mp) { mp.set(2, "z"); print(k); print(v) }
print(mp.stringify())
