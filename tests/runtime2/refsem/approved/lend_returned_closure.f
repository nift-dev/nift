// D-NEXT-refsem #1 (whole-program guard, review of ad9d842f): review h07: a callee's closure captures its
// parameter and outlives the call (the parameter escapes).
print("start")
a := [1]
fn(mk(x)) { return () => x.length() }
g := mk(a)
a.push(2)
print(g())
