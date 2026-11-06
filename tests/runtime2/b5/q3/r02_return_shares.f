// Q3 row 2 (§3 Returns): `return a` of a global gives the global object.
a := [1, 2, 3]
fn(r()) { return a }
x := r()
x.push(4)
print(a.stringify())
