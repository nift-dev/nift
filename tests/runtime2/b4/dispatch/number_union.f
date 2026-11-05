// B4: number and string to_int dispatch to their own natives.
print("before")
for (v : [3, "42", 2.0]) { print(v.to_int() + 1) }
