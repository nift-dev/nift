// D-NEXT-refsem #4 (D1), review of ad9d842f: a cycle closed by a store into a
// captured cell is refused with the catchable runtime.reference_cycle and the
// cell keeps its value (legacy copies, so it stores; an approved difference).
struct(N) { nx := ""; v := 0 }
fn(main()) {
  a := [1]
  n := N()
  n.nx = () => a.length()
  try { a = [n] } catch (e) { print("caught " + e.code) }
  print(a.length())
  return 0
}
main()
