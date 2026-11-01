// D-NEXT-refsem #4 (D1): refused cycles leave the graph unchanged and leak nothing:
// the 40-attempt run of cycle_refused.f (same live heap at stop).
struct(N) { next := ""; v := 0 }
fn(attempt()) {
    m := map()
    m.set("a", [1, 2, 3])
    f := () => m.size()
    try { m.set("f", f) } catch (e) { }
    try { m.set("s", m) } catch (e) { }
    try { m.set("arr", [m]) } catch (e) { }
    st := stack()
    try { st.push({"k": st}) } catch (e) { }
    n := N()
    g := () => n.v
    try { n.next = g } catch (e) { }
    try { n.next = n } catch (e) { }
    return m.size() + st.size() + f() + g()
}
t := 0
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
t += attempt()
print(t)
