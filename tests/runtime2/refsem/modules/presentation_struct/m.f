// Review round 3 (D-NEXT-refsem): the module's function presents a value that
// may be a struct instance or a map (the importer passes both); the program is
// refused at compile time, before the importer's first print.
fn(present_one(x)) { return x.highlight() }
export(present_one)
