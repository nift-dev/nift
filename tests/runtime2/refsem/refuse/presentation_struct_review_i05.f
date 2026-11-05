// B4: the explicit stringify() is now chosen at run time, so its result may be the user
// method's 7 or the built-in's string; `.length()` on that is refused before any effect
// (type:length: the kinds are sound, the length's receiver is not proven). B3 refused it as
// presentation-struct-method:stringify (owner ruling 2026-11-05). Legacy and the earlier B3 tree print the
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
