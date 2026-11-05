// B3 (b3-structs.md): an expression inside an imported module presents an
// instance it builds: the VM prints `C{v:0}`. Formerly refused
// (presentation-struct:stringify; review round 4, F1). Legacy fails with
// "return: undefined callable: C" (it cannot construct a module's struct
// through the exported function). An approved B3 classification change.
struct(C) {
  v := 0
}
fn(shown()) { return C().stringify() }
export(shown)
