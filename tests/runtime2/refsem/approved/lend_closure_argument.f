// D-NEXT-refsem #1 (whole-program guard, review of ad9d842f): review h01: a closure that mutates the caller's
// binding is passed along with it.
print("start")
fn(go(x, h)) { h(); print(x.stringify()); return 0 }
fn(main()) {
  a := [1]
  h := () => a.push(5)
  go(a, h)
  print(a.stringify())
  return 0
}
main()
