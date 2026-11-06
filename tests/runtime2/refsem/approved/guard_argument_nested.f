// D-NEXT-refsem #1 (D2 guard): a nested mutation of a parameter.
fn(f(p)) { p[0] = 7; return 0 }
print("start")
a := [1]
f(a)
print(a.stringify())
