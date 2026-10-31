// D-NEXT-refsem #1 (whole-program guard, review of ad9d842f): a callback calls a function that mutates the receiver.
print("start")
a := [1, 2]
fn(grow(v)) { a.push(v); return v }
b := a.map((v) => grow(v))
print(a.length() + b.length())
