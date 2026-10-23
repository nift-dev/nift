a := [1,2,3]
fs := []
for (x : a) { a = [9]; fs.push(() => x) }
f := fs[0]
print(f())
g := fs[1]
print(g())
