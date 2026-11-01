// Review round 4 (F1, D-NEXT-refsem): an expression inside an imported module
// presents an instance it builds; the program is refused at compile time,
// before the importer's first print.
struct(C) {
  v := 0
}
fn(shown()) { return C().stringify() }
export(shown)
