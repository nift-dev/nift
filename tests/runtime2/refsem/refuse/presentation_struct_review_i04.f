// B3, owner ruling (2026-11-05): refused before any effect as
// presentation-struct-method:stringify: an explicit stringify() through a
// function value on an instance of a struct declaring its own stringify, and
// on a map, needs run-time dispatch (B4). Legacy and the earlier B3 tree print
// the built-in form (`before`, 6, 7). Before B3: presentation-struct:stringify.
// The Phase A note follows.
// Review round 3 (D-NEXT-refsem), review repro inf/i04.f: a presentation method on a
// receiver that may be a struct instance: refused at compile time, before any effect.
struct(C){
  v := 0
  fn(stringify()) { return [1, 2, 3] }
}
fn(show(x)) { s := x.stringify(); print(s.length()) }
print("before")
f := show
f(C())
f(map())
