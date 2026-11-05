// B4: a parameter called with a string and an array; the result kinds feed later operations.
fn(has(v)) { r := v.contains("a"); return r }
print("before")
print(has("cat"))
print(has(["a", "b"]))
if (has("dog") == false) { print("no") }
print(has(7))
