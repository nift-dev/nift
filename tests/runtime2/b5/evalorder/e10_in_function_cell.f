// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): a function's captured local was already correct (legacy [7], [1,4]).
fn(t()) {
  b := [1]
  keep := b
  g := () => { b = [7]; return 4 }
  b.push(g())
  print(b.stringify())
  print(keep.stringify())
  return 0
}
t()
