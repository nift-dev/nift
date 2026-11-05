// B3 (docs/runtime-2/checkpoints/b3-structs.md): formerly refused before any effect
// (refsem/refuse, reason presentation-struct:stringify); the VM now renders struct instances, so this
// is a parity case: legacy and the VM print the same. The Phase A note follows.
// Review round 3 (D-NEXT-refsem), review repro repro-stringify-struct-mixed.f: a presentation method on a
// receiver that may be a struct instance: refused at compile time, before any effect.
struct(C){
  v := 0
}
print("side effect before")
xs := [map(), C()]
for (x : xs) { s := x.stringify(); print(s) }
