// B4 review M4 (t6: binding type change in a `for` callee): legacy prepares the callee and
// reports the error at another position; refused before any effect.
fn(f()) {
x := 1
x = "s"
print(x) }
for (i : [1, 2]) { f() }
