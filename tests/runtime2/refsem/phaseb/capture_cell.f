// D-NEXT-refsem #1: admitted; closures share the captured binding's cell (both
// models), so a capture is the same holder, not a second one.
fn(main()) {
  a := [1]
  g := () => a.length()
  a.push(2)
  print(g())
  h := () => a.push(3)
  h()
  print(a.length())
  return 0
}
main()
