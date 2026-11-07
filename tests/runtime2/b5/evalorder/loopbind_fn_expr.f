// H-A, review of 0d853db4 H1 (combined-b3-b5.md 3): a captured loop binding as the receiver: the expression form inside a function, and an alias keeps the element (it got the push: [1,4], [2,4]).
fn(t()) {
  for (b : [[1], [2]]) {
    keep := b
    g := () => { b = [7]; return 4 }
    n := b.push(g())
    print(b.stringify())
    print(keep.stringify())
  }
  return 0
}
t()
