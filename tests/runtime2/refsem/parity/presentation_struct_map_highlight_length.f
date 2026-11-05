// B3 (docs/runtime-2/checkpoints/b3-structs.md): formerly refused before any effect
// (refsem/refuse, reason presentation-struct:highlight); the VM now renders struct instances, so this
// is a parity case: legacy and the VM print the same. The Phase A note follows.
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
  s := x.highlight()
  print(s.length())
}
