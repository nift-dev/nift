// Review round 3 (coordinator, D-NEXT-refsem): the module declares the struct
// and returns an array holding an instance; the importer presents it. Refused
// at compile time, before the importer's first print (legacy: `[C{v:0}]`).
struct(C) {
  v := 0
}
fn(make()) { return [C()] }
export(make)
