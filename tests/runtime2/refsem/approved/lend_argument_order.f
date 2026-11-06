// D-NEXT-refsem #1 (whole-program guard, review of ad9d842f): a later argument mutates the binding an
// earlier argument lent (copy model: the copy was taken first).
print("start")
fn(show(x, n)) { print(x.length()); return n }
fn(main()) {
  a := [1]
  show(a, a.push(2))
  return 0
}
main()
