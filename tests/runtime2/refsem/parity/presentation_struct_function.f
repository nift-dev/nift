// B3 (docs/runtime-2/checkpoints/b3-structs.md): formerly refused before any effect
// (refsem/refuse, reason presentation-struct:stringify); the VM now renders struct instances, so this
// is a parity case: legacy and the VM print the same. The Phase A note follows.
// Review round 3 (D-NEXT-refsem): the struct presentation
// refusal inside a function body (the parameter may be a struct instance or a
// map): refused at compile time, before the first print.
struct(C) {
  v := 0
}
fn(show(x)) {
  s := x.stringify()
  print(s.length())
}
print("before")
show(C())
show(map())
