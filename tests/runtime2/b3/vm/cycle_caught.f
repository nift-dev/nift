// B3: after a refused cycle (catchable on the VM; legacy's error is fatal,
// the established cycle difference, refsem cell_cycle_*), rendering
// terminates and shows the unchanged acyclic graph, private fields skipped.
struct(N) {
  next := ""
  private s := "SECRET-b3"
}
a := N()
b := N()
a.next = b
try { b.next = a } catch (e) { print(e.message) }
print(a.stringify())
print(b.stringify())
print([a, b, a].prettify())
