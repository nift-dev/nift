// D-NEXT-refsem (D2 guard): fresh results may be mutated.
fn(mk()) { return [1, 2] }
x := mk()
x.push(3)
print(x.stringify())
a := [1, 2, 3]
out := a.map((v) => v * 2)
out.push(9)
print(out.stringify())
f := a.filter((v) => v > 1)
f.push(0)
print(f.stringify())
print(a.stringify())
d := deepcopy([[1]])
d.push([2])
print(d.stringify())
