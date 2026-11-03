// B2 review H2: two evaluations of one JSON-spelled object literal are two
// objects, so same() is false (objects are references: an approved difference,
// as same_object.f; legacy: an error). same() takes plain names in the VM.
fn(f(x)) { return {"k": 1} }
print("ran")
a := f(1)
b := f(2)
print(same(a, b))
print(same(a, a))
