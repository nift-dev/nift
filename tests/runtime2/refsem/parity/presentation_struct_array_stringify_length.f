// B3 (docs/runtime-2/checkpoints/b3-structs.md): formerly refused before any effect
// (refsem/refuse, reason presentation-struct:stringify); the VM now renders struct instances, so this
// is a parity case: legacy and the VM print the same. The Phase A note follows.
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
  s := x.stringify()
  print(s.length())
}
