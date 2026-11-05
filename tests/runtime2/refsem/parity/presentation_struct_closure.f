// B3 (docs/runtime-2/checkpoints/b3-structs.md): formerly refused before any effect
// (refsem/refuse, reason presentation-struct:prettify); the VM now renders struct instances, so this
// is a parity case: legacy and the VM print the same. The Phase A note follows.
// Review round 3 (D-NEXT-refsem): the struct presentation
// refusal inside a closure (it reads a value that may be a struct instance or
// a set): refused at compile time, before the first print.
struct(C) {
  v := 0
}
print("before")
xs := [C(), set()]
f := (i) => { print(xs[i].prettify()) }
f(0)
f(1)
