// B3 (owner, 2026-11-05: refused under the conservative rule), B4: an explicit
// stringify() through a function value on an instance of a struct declaring its own
// stringify, and on a map, is chosen at run time (StructIs): the struct's own method for
// the instance, the built-in for the map. Legacy ignores the method (`before`, 6, 7): an
// approved difference under the owner's explicit-call ruling. Before B4:
// presentation-struct-method:stringify; before B3: presentation-struct:stringify.
struct(C){
  v := 0
  fn(stringify()) { return [1, 2, 3] }
}
fn(show(x)) { s := x.stringify(); print(s.length()) }
print("before")
f := show
f(C())
f(map())
