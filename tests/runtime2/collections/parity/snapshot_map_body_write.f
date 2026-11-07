mp := map(); mp.set(1, "a"); mp.set(2, "b")
for ((k, v) : mp) {
    mp.set(k, v + "!")
    v = v + "?"
    print(v)
}
print(mp.stringify())
