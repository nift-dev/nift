// D-NEXT-refsem #4 (D1), review of ad9d842f: cycles closed through a captured
// cell are refused catchably and leave everything unchanged, reference counts
// and heap accounting included: 4 refused rounds here, 40 in
// cell_cycle_refused_many.f, none in cell_cycle_control.f: the same live heap at stop.
struct(N) { nx := ""; v := 0 }
fn(attempt()) {
    a := [1]
    m := map()
    m.set("f", () => a.length())
    st := stack()
    st.push(() => a.length())
    n := N()
    n.nx = () => a.length()
    set_a := () => { a = [m]; return 0 }
    try { a = [m] } catch (e) { }
    try { a.push(m) } catch (e) { }
    try { a = [st] } catch (e) { }
    try { a = [n] } catch (e) { }
    try { set_a() } catch (e) { }
    return a.length() + m.size() + st.size()
}
t := 0
t += attempt()
t += attempt()
t += attempt()
t += attempt()
print(t)
