// D-NEXT-refsem #1 (whole-program guard, review of ad9d842f): review g02/h04: a callee of the callee mutates the global.
print("start")
a := [1]
fn(g()) { a[0] = 9; return 0 }
fn(m()) { return g() }
fn(f(x)) { y := x.length(); m(); print(x[0]); return y }
f(a)
