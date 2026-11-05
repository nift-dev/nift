// B4: to_double on a number is context-dependent in legacy (loop code vs functions): refused.
print("before")
for (v : ["2.5", 3]) { print(v.to_double()) }
