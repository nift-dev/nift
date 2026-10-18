fn(d(n)) { if (n == 0) { return 0 }
return d(n - 1) + 1 }
print("s")
print(d(64))
