mp := map(); mp.set(1, "a"); mp.set(2, "b")
for ((k, v) : mp) {
    if (k == 1) { mp.remove(1); mp.set(3, "c") }
    print(k)
    print(v)
}
print(mp.stringify())
