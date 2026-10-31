mp := map(); mp.set(1, "a"); mp.set(2, "b")
for ((kk, vv) : mp) { mp.remove(2); print(kk); print(vv) }
print(mp.stringify())
