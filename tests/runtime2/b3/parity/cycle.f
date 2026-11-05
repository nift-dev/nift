// B3: a cycle through struct fields is refused before it exists (legacy's
// text); the graph rendered just before is acyclic.
struct(N) {
  next := ""
  private s := "SECRET-b3"
}
a := N()
b := N()
a.next = b
print(a.stringify())
print(b.prettify())
b.next = a
