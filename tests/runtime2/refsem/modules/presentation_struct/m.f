// B3 (b3-structs.md): the module's function presents a value that may be a
// struct instance or a map (the importer passes both): legacy and the VM print
// the same. Formerly refused (presentation-struct:highlight; review round 3).
fn(present_one(x)) { return x.highlight() }
export(present_one)
