// H-A, review of 0d853db4 H1 (combined-b3-b5.md 3): a captured loop binding as the receiver; the argument rebinds it. The push goes to the element (retained), the rebinding stays: [7] twice, and the source array shows the pushes. Legacy [7,4] twice (its prepared loop evaluator reads late); before this repair the retained receiver was written back: [1,4] [2,4].
for (b : [[1], [2]]) {
  g := () => { b = [7]; return 4 }
  b.push(g())
  print(b.stringify())
}
