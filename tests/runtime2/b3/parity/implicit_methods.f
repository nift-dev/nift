// B3, owner ruling: implicit presentation (an instance nested in a container,
// or in another instance, while that is rendered) uses the descriptor renderer
// and never runs the struct's own stringify/prettify/highlight: no
// SIDE-EFFECT line, and the counter the methods bump stays 0.
struct(C) {
  n := 0
  private s := "SECRET-b3"
  fn(stringify()) { print("SIDE-EFFECT stringify"); n = n + 1; return "user" }
  fn(prettify()) { print("SIDE-EFFECT prettify"); n = n + 1; return "user" }
  fn(highlight()) { print("SIDE-EFFECT highlight"); n = n + 1; return "user" }
}
struct(Out) {
  i := ""
  fn(Out()) { i = C() }
}
c := C()
xs := [c, c]
print(xs.stringify())
print(xs.prettify())
print(xs.highlight())
o := {"k": c}
print(o.stringify())
m := map()
m.set("k", c)
print(m.prettify())
st := stack()
st.push(c)
print(st.highlight())
w := Out()
print(w.stringify())
print(w.prettify())
print([w].stringify())
