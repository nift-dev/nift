// Q3 row 1 (§3 Call arguments): a callee mutation is visible to the caller.
a := [1, 2, 3]
fn(f(x)) { x.push(4); return 0 }
f(a)
print(a.stringify())
