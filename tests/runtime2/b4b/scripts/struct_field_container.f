// B4b: a container held in a struct field, mutated through another reference.
struct(B) { items := [] }
struct(C) { a := 1 }
b := B()
b.items.push(C())
v := b.items
v[0] = map()
print("before")
print(b.items[0].a)
