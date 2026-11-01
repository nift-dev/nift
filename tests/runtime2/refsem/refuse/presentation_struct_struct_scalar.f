// Review round 4 (F1, D-NEXT-refsem): a presentation method on a value
// whose inferred kinds include a struct instance (a struct instance or a number) is refused at
// compile time from the receiver's kinds, whatever the expression, before
// the first print (legacy renders `C{v:0}`). It stopped after output with an
// internal failure. A compatibility gap to eliminate (Phase B / VM struct
// rendering), not intended behaviour.
struct(C) {
  v := 0
}
print("before")
xs := [C(), 5]
for (x : xs) {
  print(x.stringify())
}
