// D-NEXT-refsem #4 (D1), review round 2: every array mutator on a captured
// cell (insert, splice, an expression push, an element store, a statement
// push) refuses a cycle before the value leaves the cell; afterwards the
// array is intact and later reads, mutations and cleanup behave: 4 rounds here, 40 in
// cell_mutator_refused_many.f, none in cell_mutator_control.f: the same live heap.
fn(attempt()) {
    a := [1]
    st := stack()
    st.push(() => a.length())
    try { a.insert(0, st) } catch (e) { }
    try { a.splice(0, 0, [st]) } catch (e) { }
    try { x := a.push(st) } catch (e) { }
    try { a[0] = st } catch (e) { }
    try { a.push(st) } catch (e) { }
    a.push(2)
    a[0] = 7
    s := a.stringify()
    return a.length() + st.size() + s.length()
}
t := 0
t += attempt()
t += attempt()
t += attempt()
t += attempt()
print(t)
