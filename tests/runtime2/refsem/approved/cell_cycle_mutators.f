// D-NEXT-refsem #4 (D1), review round 2 (review c/cx, c6, c7, c9): a cycle
// closed through a captured array by insert, splice, an expression push or an
// element store is refused catchably before the array leaves its cell; the
// array keeps its value and later reads and mutations work (legacy copies, so
// it stores; an approved difference).
fn(main()) {
  a := [1]
  st := stack()
  st.push(() => a.length())
  try { a.insert(0, st) } catch (e) { print("insert " + e.code) }
  print(a.stringify())
  try { a.splice(0, 0, [st]) } catch (e) { print("splice " + e.code) }
  print(a.stringify())
  try { x := a.push(st) } catch (e) { print("push-expr " + e.code) }
  print(a.stringify())
  try { a[0] = st } catch (e) { print("index " + e.code) }
  print(a.stringify())
  a.insert(0, 5)
  a.splice(1, 0, [6])
  a.push(7)
  print(a.stringify())
  print(st.size())
  return 0
}
main()
