// H-A (docs/runtime-2/checkpoints/combined-b3-b5.md): a top-level binding used in a function, rebound by a `fn` (legacy [7,8], [1,2,4]).
b := [1, 2]
keep := b
fn(g()) { b = [7, 8]; return 4 }
fn(t()) {
  b.push(g())
  b.insert(0, g())
  return 0
}
t()
print(b.stringify())
print(keep.stringify())
