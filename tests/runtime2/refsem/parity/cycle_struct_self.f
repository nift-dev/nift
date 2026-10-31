// D-NEXT-refsem #4: a struct field closing a cycle (legacy text).
struct(N) { next := "" }
a := N()
print("before")
a.next = a
print("never")
