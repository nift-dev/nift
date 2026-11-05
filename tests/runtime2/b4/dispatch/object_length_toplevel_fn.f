// B4: a function called from top level only runs canonically in legacy too: an object's
// length() is the malformed-expression error in both engines (still compiled).
fn(f(v)) { print(v.length()) }
print("before")
f("abc")
f({"a": 1})
