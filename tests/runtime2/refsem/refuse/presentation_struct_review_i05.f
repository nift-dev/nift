// Review round 3 (D-NEXT-refsem), review repro inf/i05.f: a presentation method on a
// receiver that may be a struct instance: refused at compile time, before any effect.
struct(C){
  v := 0
  fn(stringify()) { return 7 }
}
print("before")
xs := [C(), map()]
for (x : xs) { s := x.stringify(); print(s.length()) }
