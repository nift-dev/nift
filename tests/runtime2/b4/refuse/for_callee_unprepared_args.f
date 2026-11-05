// B4 review M4 (t2: unpreparable callee's argument evaluated twice): legacy prepares a function called from a `for` loop, as it
// does for a `while` loop's callee; the prepared behaviour differs here, so refused before any effect.
fn(g(x)) { print(x)
return x }
fn(f(p)) { return (true ? p : 0) }
for (i : [1]) { f(g(1)) }
