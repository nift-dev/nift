// B4: a failing family with an observable argument: legacy never evaluates it; refused.
fn(sep()) { print("sep"); return "," }
print("before")
for (v : [[1, 2], "ab"]) { print(v.join(sep())) }
