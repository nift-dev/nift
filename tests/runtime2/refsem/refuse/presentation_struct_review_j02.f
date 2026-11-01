// Review round 3 (D-NEXT-refsem), review repro inf/j02.f: a presentation method on a
// receiver that may be a struct instance: refused at compile time, before any effect.
struct(C){
  v := 0
}
print("before")
xs := [C(), map()]
for (x : xs) { s := x.stringify(); print(s.length()); print(s) }
