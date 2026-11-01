// Review round 3 (coordinator, D-NEXT-refsem): a set holding an instance
// cannot be built on the VM (a struct as a set member is refused before any
// effect, D-NEXT-refsem #6), so its presentation is never reached.
struct(C) {
  v := 0
}
print("before")
s := set()
s.add(C())
print(s.stringify())
