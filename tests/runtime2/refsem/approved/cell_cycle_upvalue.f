// D-NEXT-refsem #4 (D1), review of ad9d842f: a cycle closed by a store into a
// captured cell is refused with the catchable runtime.reference_cycle and the
// cell keeps its value (legacy copies, so it stores; an approved difference).
fn(main()) {
  a := [1]
  m := map()
  m.set("f", () => a.length())
  set_a := () => { a = [m]; return 0 }
  try { set_a() } catch (e) { print("caught " + e.code) }
  print(a.length())
  return 0
}
main()
