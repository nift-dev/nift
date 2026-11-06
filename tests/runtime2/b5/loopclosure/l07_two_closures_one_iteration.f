// B5 D1 (owner 2026-11-07 06:55, decision 1): two closures of one iteration, created before and after an assignment, both see the final value; iterations independent
fs := []
for (x : [1, 2]) {
  f := () => x
  fs.push(f)
  x = x + 5
  fs.push(() => x * 10)
}
a := fs[0]
b := fs[1]
c := fs[2]
d := fs[3]
print(a())
print(b())
print(c())
print(d())
