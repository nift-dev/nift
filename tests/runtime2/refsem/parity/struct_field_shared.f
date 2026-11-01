// D-NEXT-refsem #1 (D4): a struct field holding an instance shares it (seen through same()).
struct(N) { next := ""; v := 1 }
a := N()
b := N()
a.next = b
x := a.next
print(same(x, b))
b.v = 7
y := a.next
print(same(x, y))
