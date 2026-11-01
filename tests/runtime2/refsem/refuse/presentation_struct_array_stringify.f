// Review round 3 (D-NEXT-refsem): `stringify()` on a value that may be a
// struct instance or an array needs dynamic dispatch of the presentation method
// (the struct's rendering or method, or the collection's), which the VM lacks:
// refused at compile time, before the first print. A Phase B / later VM
// capability candidate, not a permanent limitation.
struct(C) {
  v := 0
}
print("before")
xs := [C(), [1]]
for (x : xs) {
  print(x.stringify())
}
