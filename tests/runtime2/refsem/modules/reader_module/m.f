// D-NEXT-refsem #1: admitted; a module function only reads its parameter
// and references no importer binding.
fn(count_of(x)) { return x.length() }
export(count_of)
