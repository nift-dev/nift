fn(d(n, s)) { t := s + "xxxxxxxx"
if (n == 0) { return 0 }
return d(n - 1, t) + 1 }
print("s")
print(d(99990, "a"))
