// D-NEXT-refsem #4 (D1), review round 3: a splice that removes (count > 0)
// and whose replacement closes a cycle through a captured array is refused
// before anything is removed, in statement, expression and closure forms;
// afterwards the array is intact and later reads, mutations and cleanup behave:
// the control (no refused splice).
fn(attempt()) {
    a := [1, 2, 3]
    st := stack()
    st.push(() => a.length())
    a.push(4)
    s := a.stringify()
    return a.length() + st.size() + s.length()
}
t := 0
t += attempt()
t += attempt()
t += attempt()
t += attempt()
print(t)
