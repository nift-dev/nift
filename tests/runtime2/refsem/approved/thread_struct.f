// D-NEXT-refsem #5: an instance crosses a thread as a copy (legacy refuses).
struct(P) { v := 0 }
fn(w(p)) { p.v = 9; return p.v }
a := P()
t := thread(w, a)
print(t.join())
print(a.v)
