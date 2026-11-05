// B4: a string-or-array receiver dispatches `contains` to the native of its run-time family.
print("before")
for (v : ["xa", ["a"], "b", [1]]) { print(v.contains("a")) }
