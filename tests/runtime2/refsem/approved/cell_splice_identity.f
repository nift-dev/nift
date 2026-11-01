// D-NEXT-refsem #4 (D1), review round 3 (owner): the captured-array splice
// builds its result on a copy of the array and commits it with one store.
// The copy shares every nested mutable reference (map, set, stack, struct
// instance): after a successful splice, a mutation through an alias is
// visible through the receiver (the element read back is the same object); after a refused splice, the nested elements
// are the same objects with the same contents, and the receiver is unchanged.
// Legacy copies, so it stores the cycle and later reads differ (an approved
// difference).
struct(C) {
  v := 0
}
fn(main()) {
  m := map()
  m.set("k", 1)
  s := set()
  s.add(1)
  q := stack()
  q.push(1)
  c := C()
  a := [0, m, s, q, c]
  st := stack()
  st.push(() => a.length())
  a.splice(0, 1, [7, 8])
  m.set("k", 2)
  s.add(2)
  q.push(2)
  c.v = 5
  x := a[2]
  s1 := a[3]
  q1 := a[4]
  y := a[5]
  print(same(x, m))
  print(same(s1, s))
  print(same(q1, q))
  print(same(y, c))
  print(m.get("k"))
  print(c.v)
  try { a.splice(0, 2, [m, s, q, c, st]) } catch (e) { print("refused " + e.code) }
  print(a.length())
  print(a[0] == 7)
  print(a[1] == 8)
  x2 := a[2]
  s2 := a[3]
  q2 := a[4]
  y2 := a[5]
  print(same(x2, m))
  print(same(s2, s))
  print(same(q2, q))
  print(same(y2, c))
  print(m.get("k"))
  print(s.size())
  print(q.size())
  print(c.v)
  print(a.length())
  return 0
}
main()
