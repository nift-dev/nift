// D-NEXT-refsem #4 (D1): an instance field holding a closure that captures the instance.
struct(P) { f := ""; v := 0 }
fn(make()) {
    p := P()
    g := () => p.v
    try { p.f = g } catch (e) { print(e.message) }
    print(p.f == "")
    return 0
}
make()
