// D-NEXT-refsem #4 (D1), review of ad9d842f: cycles closed through a captured
// cell are refused catchably and leave everything unchanged, reference counts
// and heap accounting included: the control of cell_cycle_refused.f
// (the same program without the refused stores).
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
    return a.length() + m.size() + st.size()
}
t := 0
t += attempt()
t += attempt()
t += attempt()
t += attempt()
print(t)
