// Review round 3 (D-NEXT-refsem): the mixed struct/collection presentation
// refusal inside a function body (the parameter may be a struct instance or a
// map): refused at compile time, before the first print.
struct(C) {
  v := 0
}
fn(show(x)) {
  s := x.stringify()
  print(s.length())
}
print("before")
show(C())
show(map())
