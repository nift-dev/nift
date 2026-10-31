// D-NEXT-refsem #4 (D1): a collection holding a closure that captures it is refused (legacy accepts).
// Inside a function the map is a captured local (a cell), so the store would close a heap cycle.
fn(make()) {
    m := map()
    f := () => m.size()
    try { m.set("f", f) } catch (e) { print(e.message) }
    print(m.size())
    s := set()
    g := () => s.size()
    try { s.add(g) } catch (e) { print(e.message) }
    print(s.size())
    return 0
}
make()
