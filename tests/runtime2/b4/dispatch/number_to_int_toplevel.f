// B4: a fraction's to_int() at top level and in a function called from top level reports
// at the call in both engines (compiled).
fn(f(v)) { print(v.to_int()) }
print("before")
f(3)
f(2.5)
