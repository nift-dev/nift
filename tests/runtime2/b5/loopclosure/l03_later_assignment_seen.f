// B5 D1 (owner 2026-11-07 06:55, decision 1): a later assignment to the binding in the same iteration is observed by that iteration's closure
fs := []
for (x : [1, 2]) {
  fs.push(() => x)
  x = x * 100
}
a := fs[0]
b := fs[1]
print(a())
print(b())
