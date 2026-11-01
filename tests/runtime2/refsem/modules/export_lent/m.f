// D-NEXT-refsem #1, review mod2: a module's exported array is mutated by
// the module's own function while the importer's argument aliases it.
data := [1]
fn(add(v)) { data.push(v); return 0 }
fn(peek(x)) { add(7); return x.length() }
export(data)
export(add)
export(peek)
