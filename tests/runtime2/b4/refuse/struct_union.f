// B4: a union with a struct instance keeps its refusal through a NAMED receiver: legacy says
// "struct has no method: trim" there (B5 item B admits element/member receivers only).
struct(C) { a := 1 }
fn(f(v)) { return v.trim() }
print("before")
print(f(" a "))
print(f(C()))
