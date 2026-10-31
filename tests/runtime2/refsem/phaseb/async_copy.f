// D-NEXT-refsem #1: admitted; an async call's arguments are copied at task
// creation in both models (D7), so a later mutation is not seen by the task.
fn[async](w(x)) { return x.length() }
fn(main()) {
  a := [1]
  f := w(a)
  a.push(2)
  print(await f)
  print(a.length())
  return 0
}
main()
