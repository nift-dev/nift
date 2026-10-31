// D-NEXT-refsem #4 (D1): a refused struct cycle is catchable; the field is unchanged.
struct(N) { next := ""; v := 1 }
a := N()
try {
    a.next = a
} catch (e) {
    print(e.code)
    print(e.message)
}
print(a.next == "")
