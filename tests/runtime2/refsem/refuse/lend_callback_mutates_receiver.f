// D-NEXT-refsem #1 (whole-program guard, review of ad9d842f): a native's callback
// mutates the receiver it iterates.
print("start")
fn(main()) {
  a := [1, 2]
  b := a.map((v) => v + a.push(0))
  print(b.stringify())
  print(a.length())
  return 0
}
main()
