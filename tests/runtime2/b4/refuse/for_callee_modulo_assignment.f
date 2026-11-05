// B4 review M4 (t7: `x %= 2` on a double): legacy prepares a function called from a `for` loop, as it
// does for a `while` loop's callee; the prepared behaviour differs here, so refused before any effect.
fn(f()) { x := 7.5
x %= 2
print(x) }
for (i : [1]) { f() }
