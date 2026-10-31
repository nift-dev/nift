// D-NEXT-refsem #1 (D4): a struct argument is the caller instance.
struct(P) { v := 0 }
fn(f(p)) { p.v = 9; return 0 }
a := P()
f(a)
print(a.v)
