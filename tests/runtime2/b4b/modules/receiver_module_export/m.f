// B4b 32 (review of ce0a9a44, N-1): a module's own array, sorted inside the module through an
// alias of the result; the importer reads it back through an exported function.
xs := [" a ", " b "]
fn(spoil()) {
  ys := xs.sort()
  ys.push(5)
  return 0
}
fn(show()) {
  for (e : xs) { print(e.trim()) }
  return 0
}
export(spoil)
export(show)
