// D-NEXT-refsem #4, review round 4 (F2; coordinator ruling, owner-approved):
// every splice evaluates the receiver and all its arguments, then validates
// (bounds, replacement, cycle), prepares the result without touching the
// original, and commits once. A throwing argument leaves the array unchanged,
// whether the array is a plain local, captured by a closure, a global, used
// inside a closure, or the splice is an expression. Side effects of the
// argument's own evaluation are not rolled back (`log` keeps its push).
// Legacy removes before evaluating the replacement ([3]): an approved
// difference.
log := []
fn(boom(tag)) {
  log.push(tag)
  throw error("boom")
}
g := [1, 2, 3]
try { g.splice(0, 2, [boom("global")]) } catch (e) { print("global " + e.message) }
print(g.stringify())
try { x := g.splice(0, 2, [7, boom("global-expr")]) } catch (e) { print("global-expr " + e.message) }
print(g.stringify())
fn(main()) {
  a := [1, 2, 3]
  try { a.splice(0, 2, [boom("local")]) } catch (e) { print("local " + e.message) }
  print(a.stringify())
  try { y := a.splice(1, 1, [boom("local-expr")]) } catch (e) { print("local-expr " + e.message) }
  print(a.stringify())
  b := [1, 2, 3]
  f := () => b.length()
  try { b.splice(0, 2, [boom("captured")]) } catch (e) { print("captured " + e.message) }
  print(b.stringify())
  c := [1, 2, 3]
  h := () => { try { c.splice(0, 2, [5, boom("closure")]) } catch (e) { print("closure " + e.message) } }
  h()
  print(c.stringify())
  print(f())
  return 0
}
main()
print(log.length())
print(log.stringify())
