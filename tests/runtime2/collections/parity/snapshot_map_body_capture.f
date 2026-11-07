mp := map(); mp.set(1, "a"); mp.set(2, "b")
fs := []
for ((k, v) : mp) {
    mp.set(k, v + "!")
    fs.push(() => v)
}
f0 := fs[0]
f1 := fs[1]
print(f0())
print(f1())
print(mp.stringify())
