// Review round 3 (D-NEXT-refsem): the mixed struct/collection presentation
// refusal inside a closure (it reads a value that may be a struct instance or
// a set): refused at compile time, before the first print.
struct(C) {
  v := 0
}
print("before")
xs := [C(), set()]
f := (i) => { print(xs[i].prettify()) }
f(0)
f(1)
