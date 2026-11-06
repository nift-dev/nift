// B5 D1 (owner 2026-11-07 06:55, decision 1): each iteration has a distinct binding; closures escape the loop and run after it ends
fs := []
for (x : [1, 2, 3]) {
  fs.push(() => x)
}
f0 := fs[0]
f1 := fs[1]
f2 := fs[2]
print(f0())
print(f1())
print(f2())
