// D-NEXT-refsem #1: construction anywhere a value is.
struct(P) { v := 0 }
fn(mk()) { return P() }
a := mk()
b := mk()
print(same(a, b))
print(a == b)
c := [P(), P()]
x := c[0]
x.v = 1
y := c[1]
print(y.v)
