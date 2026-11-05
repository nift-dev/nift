// B4: when every family has the native, arguments may have effects (evaluated once, before).
fn(key()) { print("key"); return "a" }
print("before")
for (v : ["xa", ["a"]]) { print(v.contains(key())) }
