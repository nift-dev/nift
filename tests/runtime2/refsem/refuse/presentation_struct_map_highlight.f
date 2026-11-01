// Review round 3 (D-NEXT-refsem): `highlight()` on a value that may be a
// struct instance or a map needs dynamic dispatch of the presentation method
// (the struct's rendering or method, or the collection's), which the VM lacks:
// refused at compile time, before the first print. A Phase B / later VM
// capability candidate, not a permanent limitation.
struct(C) {
  v := 0
}
print("before")
xs := [C(), map()]
for (x : xs) {
  print(x.highlight())
}
