// Review round 3 (D-NEXT-refsem): `stringify()` on a value that may be a
// struct instance or a set needs dynamic dispatch of the presentation method
// (the struct's rendering or method, or the collection's), which the VM lacks:
// refused at compile time, before the first print. A Phase B / later VM
// capability candidate, not a permanent limitation.
struct(C) {
  v := 0
}
print("before")
xs := [C(), set()]
for (x : xs) {
  s := x.stringify()
  print(s.length())
}
