// D-NEXT-refsem #1 (whole-program guard, review of ad9d842f): a closure returned from a callee of the callee
// captures the forwarded parameter.
print("start")
fn(mk(x)) { return () => x.length() }
fn(wrap(y)) { return mk(y) }
fn(main()) {
  a := [1]
  g := wrap(a)
  a.push(2)
  print(g())
  return 0
}
main()
