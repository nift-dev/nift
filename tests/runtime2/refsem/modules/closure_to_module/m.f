// D-NEXT-refsem #1, review mod3: a module function runs the importer's
// closure, which mutates the array it was also given.
fn(peek(x, h)) { h(); return x.length() }
export(peek)
