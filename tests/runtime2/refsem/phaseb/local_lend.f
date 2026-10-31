// D-NEXT-refsem #1: admitted; a local is lent to a callee that only reads it, and is
// mutated only between calls by its own frame: Phase B (the callee aliases it)
// prints the same.
fn(len2(x)) { return x.length() }
fn(main()) {
  a := [1]
  a.push(2)
  print(len2(a))
  a.push(3)
  print(len2(a))
  return 0
}
main()
