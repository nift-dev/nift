// B3: instances inside arrays, objects, maps and stacks, compact and pretty.
struct(C) {
  a := 1
  private b := "SECRET-b3"
}
c := C()
xs := [c, 2, c]
print(xs.stringify())
print(xs.prettify())
o := {"x": c, "y": [c]}
print(o.stringify())
print(o.prettify())
m := map()
m.set("k", c)
m.set(2, [c])
print(m.stringify())
print(m.prettify())
s := stack()
s.push(c)
s.push(3)
print(s.stringify())
print(s.prettify())
