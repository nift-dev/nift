// D-NEXT-refsem: a field initialised with "" holding an instance is string-or-struct to the
// flow-insensitive inference: field access through a value read from it is refused (sound).
struct(N) { next := ""; v := 1 }
a := N()
b := N()
print("start")
a.next = b
x := a.next
x.v = 7
print(b.v)
