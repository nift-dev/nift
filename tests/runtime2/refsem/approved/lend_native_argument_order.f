// D-NEXT-refsem #1 (whole-program guard, review of ad9d842f): a native's argument
// mutates the receiver it reads (copy model: -1, the receiver read first;
// Phase B: 1).
print("start")
fn(main()) {
  a := [1]
  print(a.indexOf(a.push(2)))
  return 0
}
main()
