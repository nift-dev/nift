fn(d(n)) { if (n == 0) { return 1 / n }
return d(n - 1) }
print("s")
print(d(100005))
