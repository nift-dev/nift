// B4: a union with a struct instance keeps its refusal (struct dispatch is not a plain family).
struct(C) { a := 1 }
fn(f(v)) { return v.trim() }
print("before")
print(f(" a "))
print(f(C()))
