// D-NEXT-refsem #4: a two-instance cycle (legacy text).
struct(N) { next := "" }
a := N()
b := N()
a.next = b
print("before")
b.next = a
