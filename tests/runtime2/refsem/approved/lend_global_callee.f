// D-NEXT-refsem #1 (whole-program guard, review of ad9d842f): review g01: the callee mutates the global its
// parameter aliases.
print("start")
a := [1]
fn(f(x)) { a.push(2); print(x.stringify()); return 0 }
f(a)
print(a.stringify())
