// B3, owner ruling (2026-11-05): refused before any effect as
// presentation-struct-method:stringify: an explicit stringify() on a value that
// may be an instance of a struct declaring its own stringify (here returning 7)
// needs run-time dispatch (B4). Legacy and the earlier B3 tree print the
// built-in form (`before`, 6 and 7: the lengths of C{v:0} and map([])). Before B3:
// presentation-struct:stringify. The Phase A note follows.
// Review round 3 (D-NEXT-refsem), review repro inf/i05.f: a presentation method on a
// receiver that may be a struct instance: refused at compile time, before any effect.
struct(C){
  v := 0
  fn(stringify()) { return 7 }
}
print("before")
xs := [C(), map()]
for (x : xs) { s := x.stringify(); print(s.length()) }
