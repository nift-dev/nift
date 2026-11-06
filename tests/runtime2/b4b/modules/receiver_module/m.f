// B4b 32 (review of ce0a9a44, N-1): a module function returns its parameter's reverse(); the
// importer stores through the result and reads the original array.
fn(rev(a)) { return a.reverse() }
export(rev)
