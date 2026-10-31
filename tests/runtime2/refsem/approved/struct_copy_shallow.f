// D-NEXT-refsem #3 (D3): copy() of an instance is a new instance whose fields are shared.
struct(P) { v := 0 }
struct(Q) { a := ""; b := "" }
p := P()
q := Q()
q.a = p
q.b = p
r := copy(q)
print(same(r, q))
x := r.a
print(same(x, p))
d := deepcopy(q)
y := d.a
z := d.b
print(same(y, z))
print(same(y, p))
