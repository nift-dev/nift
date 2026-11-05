// B3 (docs/runtime-2/checkpoints/b3-structs.md): formerly refused before any effect
// (refsem/refuse, reason presentation-struct:highlight); the VM now renders struct instances, so this
// is a parity case: legacy and the VM print the same. The Phase A note follows.
// Review round 4 (F1, D-NEXT-refsem): a presentation method on a value
// whose inferred kinds include a struct instance (a map lookup) is refused at
// compile time from the receiver's kinds, whatever the expression, before
// the first print (legacy renders `C{v:0}`). It stopped after output with an
// internal failure. A compatibility gap to eliminate (Phase B / VM struct
// rendering), not intended behaviour.
struct(C) {
  v := 0
}
print("before")
m := map()
m.set("k", C())
print(m.get("k").highlight())
