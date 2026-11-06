// §3 Returns/arguments: a function returning its parameter returns the caller object.
fn(id(v)) { return v }
a := [1]
b := id(a)
b.push(2)
print(a.stringify())
