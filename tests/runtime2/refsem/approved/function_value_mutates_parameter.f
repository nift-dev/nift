// D-NEXT-refsem #1 (whole-program guard, review of ad9d842f): a function used as a value (a native's
// callback) mutates its parameter: its callers, and their values, are unseen.
print("start")
fn(fill(r)) { r.push(0); return r.length() }
a := [[1], [2]]
b := a.map(fill)
print(a.stringify())
