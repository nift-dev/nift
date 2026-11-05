// B4: an object-or-array receiver: `keys` on the object, then the array's ordinary error.
print("before")
for (v : [{"a": 1, "b": 2}, [1]]) { print(v.keys().join(",")) }
