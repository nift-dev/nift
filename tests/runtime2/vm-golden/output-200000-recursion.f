fn(d(n)) { if (n == 0) { return 0 }
return d(n - 1) }
p := "xxxxxxxxxxxxxxxxxxxx"
i0 := 0
while (i0 < 200000) { print(p + i0); i0 += 1 }
print(d(70))
