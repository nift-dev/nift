a := range(1000000)
i := 0
for (x : a) { a[i] = a[i] + 1; i += 1 }
print(a[999999])
