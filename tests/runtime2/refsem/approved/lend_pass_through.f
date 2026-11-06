// D-NEXT-refsem #1 (whole-program guard, review of ad9d842f): review i08: the parameter is passed on to a
// second function, which mutates the global while it reads its own parameter.
print("start")
a := [1, 2]
fn(g(y)) { a.pop(); return y.length() }
fn(f(x)) { return g(x) }
print(f(a))
