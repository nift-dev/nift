// B4 review M4 (t3: assignment error position): legacy prepares a function called from a `for` loop, as it
// does for a `while` loop's callee; the prepared behaviour differs here, so refused before any effect.
fn(f(a)) {
x := 1
x = a
return x }
for (i : [1]) { print(f("s")) }
