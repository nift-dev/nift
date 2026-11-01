mp := map(); mp.set(1, "a"); mp.set(2, "b"); mp.set(3, "c")
n := 0
for ((k, v) : mp) {
    if (k == 1) { mp.remove(2) }
    print(k)
    print(v)
    n = n + 1
    if (n == 2) { break }
}
print(mp.stringify())
