// B2 review H1 (case table: copy/deepcopy of an evaluated literal): both are
// new objects (same() false) with equal contents (== true). Objects are
// references, so same() answers identity (approved difference; legacy: an error).
fn(f(x)) { return {"k": [1]} }
print("ran")
a := f(1)
c := copy(a)
d := deepcopy(a)
print(same(a, c))
print(same(a, d))
print(a == c)
print(a == d)
