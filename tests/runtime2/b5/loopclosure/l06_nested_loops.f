// B5 D1 (owner 2026-11-07 06:55, decision 1): nested loops: each closure keeps its own outer and inner bindings
fs := []
for (x : [1, 2]) {
  for (y : [10, 20]) {
    fs.push(() => x * 100 + y)
  }
}
a := fs[0]
b := fs[1]
c := fs[2]
d := fs[3]
print(a())
print(b())
print(c())
print(d())
