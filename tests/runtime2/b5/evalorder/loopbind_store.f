// H-A, review of 0d853db4 H1 (combined-b3-b5.md 3): a captured loop binding as the receiver: element store, value first (legacy [7,8]; 17f7964c and before this repair [5,2]).
for (b : [[1, 2]]) {
  g := () => { b = [7, 8]; return 5 }
  b[0] = g()
  print(b.stringify())
}
