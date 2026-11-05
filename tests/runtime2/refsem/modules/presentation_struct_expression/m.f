// B3 (b3-structs.md): an expression inside an imported module presents an
// instance it builds: the VM prints `C{v:0}`. Formerly refused
// (presentation-struct:stringify; review round 4, F1). Legacy fails with
// "return: undefined callable: C" (it cannot construct a module's struct
// through the exported function; a difference that predates B3,
// compatibility.md). A B3 classification change APPROVED BY THE OWNER, 2026-11-05
// (correct VM behaviour; a documented compatibility difference).
struct(C) {
  v := 0
}
fn(shown()) { return C().stringify() }
export(shown)
