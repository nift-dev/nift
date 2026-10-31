// D-NEXT-refsem #1, review mod4: the importer lends the module's exported
// array to its own function, which calls the module's mutator.
data := [1]
fn(add(v)) { data.push(v); return 0 }
export(data)
export(add)
