// H-A, review of 0d853db4 H1 (combined-b3-b5.md 3): a captured loop binding as the receiver: insert and remove (legacy [7,8] twice; before this repair [9,1,2] [1,2]).
for (b : [[1, 2]]) {
  g := () => { b = [7, 8]; return 0 }
  b.insert(g(), 9)
  print(b.stringify())
  b.remove(g())
  print(b.stringify())
}
