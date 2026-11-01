// D-NEXT-refsem #5 (D7): results never alias the caller; awaiting twice gives the same result.
fn[async](w(a)) { return [a] }
m := map()
f := w(m)
r1 := await f
r2 := await f
x := r1[0]
y := r2[0]
print(same(x, y))
print(same(x, m))
x.set(2, 2)
print(m.size())
print(y.size())
