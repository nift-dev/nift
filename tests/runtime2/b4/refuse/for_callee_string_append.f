// B4 review M4 (t8: `s += "b"`): legacy prepares a function called from a `for` loop, as it
// does for a `while` loop's callee; the prepared behaviour differs here, so refused before any effect.
fn(f()) { s := "a" + 0.5
s += "b"
print(s) }
for (i : [1]) { f() }
