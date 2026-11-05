// B4 (b4-dispatch.md 2): an object's length() in a function called from a `for` loop.
// Legacy runs that body on its prepared evaluator, which counts the members (1); the VM
// compiled the canonical behaviour (a malformed-expression error): now refused before any
// effect, as it already was for a `while` loop's callee.
fn(f(v)) { print(v.length()) }
print("before")
o := {"a": 1}
for (i : [1]) { f(o) }
