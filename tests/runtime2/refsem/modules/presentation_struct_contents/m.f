// B3 (b3-structs.md): the module declares the struct and returns an array
// holding an instance; the importer presents it. The VM prints `[C{v:0}]`.
// Formerly refused (presentation-struct-contents:stringify; review round 3);
// the note there that legacy prints `[C{v:0}]` was wrong: legacy fails with
// "return: undefined callable: C" (it cannot construct a module's struct
// through the exported function). An approved B3 classification change.
struct(C) {
  v := 0
}
fn(make()) { return [C()] }
export(make)
