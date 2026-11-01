// D-NEXT-refsem #4 (D1), review round 2: every array mutator on a captured
// cell (insert, splice, an expression push, an element store, a statement
// push) refuses a cycle before the value leaves the cell; afterwards the
// array is intact and later reads, mutations and cleanup behave: the control (no refused store).
fn(attempt()) {
    a := [1]
    st := stack()
    st.push(() => a.length())
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
