// B4: to_string on an array in a number-or-array union: legacy's composite-value error.
print("before")
for (v : [5, 2.5, [1]]) { print(v.to_string()) }
