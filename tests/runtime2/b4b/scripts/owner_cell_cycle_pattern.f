// B4b 26 (review of 4c9632b8, H-1): the corpus instance's pattern (refsem/heap/cell_cycle_control.f):
// a function's local reassigned by a lambda to an array holding a value whose kind is known only
// later; here the lambda is called, so the function's read must see the int.
fn(d2()) { return 7 }
fn(d1()) { return d2() }
fn(attempt()) {
    a := [" x "]
    set_a := () => { a = [d1()]; return 0 }
    set_a()
    return a[0].trim()
}
print(attempt())
