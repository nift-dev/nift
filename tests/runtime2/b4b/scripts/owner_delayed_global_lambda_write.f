// B4b 26 (review of 4c9632b8, H-1, reviewer's U13a): the growth from a global's element store reached through a call chain
fn(d3()) { return 5 }
fn(d2()) { return d3() }
fn(d1()) { return d2() }
src := [" s "]
fn(grow()) {
    src[0] = d1()
    return 0
}
fn(attempt()) {
    a := [" x "]
    set_a := () => { a = [src[0]]; return 0 }
    set_a()
    return a[0].trim()
}
print(attempt())
grow()
print(attempt())
