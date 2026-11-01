// D-NEXT-refsem #4 (D1), review round 3: a splice that removes elements
// (count > 0) and whose replacement list closes a cycle through a captured
// array is refused before anything changes. The whole proposed mutation (the
// removal and every replacement) is validated first: when only the LAST of
// several replacements closes the cycle, neither the removal nor an earlier
// replacement takes effect, and the receiver and everything reachable from
// it are byte-for-byte what they were. Statement, expression, inside a
// closure, and through a second captured cell. Legacy copies, so it stores
// (an approved difference). The state is rendered element by element (a stack by
// its type, so legacy's spliced-in stack shows rather than failing to serialize).
fn(main()) {
  a := [1, 2, 3, [10]]
  st := stack()
  st.push(() => a.length())
  r := ""
  for (x : a) { if (type(x) == "collection") { r += "<stack>," } else { r += x.stringify() + "," } }
  before := r
  try { a.splice(0, 2, [st]) } catch (e) { print("statement " + e.code) }
  r = ""
  for (x : a) { if (type(x) == "collection") { r += "<stack>," } else { r += x.stringify() + "," } }
  print(r == before)
  try { a.splice(1, 2, [5, 6, st]) } catch (e) { print("statement-last " + e.code) }
  r = ""
  for (x : a) { if (type(x) == "collection") { r += "<stack>," } else { r += x.stringify() + "," } }
  print(r == before)
  try { x := a.splice(0, 3, [st]) } catch (e) { print("expression " + e.code) }
  r = ""
  for (x : a) { if (type(x) == "collection") { r += "<stack>," } else { r += x.stringify() + "," } }
  print(r == before)
  try { x := a.splice(0, 1, [7, [8], 9, st]) } catch (e) { print("expression-last " + e.code) }
  r = ""
  for (x : a) { if (type(x) == "collection") { r += "<stack>," } else { r += x.stringify() + "," } }
  print(r == before)
  f := () => {
    try { a.splice(0, 4, [st]) } catch (e) { print("closure " + e.code) }
    try { a.splice(2, 1, [11, 12, st]) } catch (e) { print("closure-last " + e.code) }
    try { y := a.splice(1, 2, [13, st]) } catch (e) { print("closure-expr " + e.code) }
  }
  f()
  r = ""
  for (x : a) { if (type(x) == "collection") { r += "<stack>," } else { r += x.stringify() + "," } }
  print(r == before)
  holder := stack()
  b := [20, 21, 22]
  holder.push(() => b.length())
  g := () => {
    try { b.splice(0, 2, [23, 24, holder]) } catch (e) { print("captured-last " + e.code) }
  }
  g()
  r = ""
  for (x : b) { if (type(x) == "collection") { r += "<stack>," } else { r += x.stringify() + "," } }
  print(r)
  r = ""
  for (x : a) { if (type(x) == "collection") { r += "<stack>," } else { r += x.stringify() + "," } }
  print(r)
  print(a.length())
  print(a[3].stringify())
  print(st.size())
  print(holder.size())
  a.splice(0, 2, [30, 31, 32])
  b.splice(1, 1, [40])
  r = ""
  for (x : a) { if (type(x) == "collection") { r += "<stack>," } else { r += x.stringify() + "," } }
  print(r)
  r = ""
  for (x : b) { if (type(x) == "collection") { r += "<stack>," } else { r += x.stringify() + "," } }
  print(r)
  return 0
}
main()
