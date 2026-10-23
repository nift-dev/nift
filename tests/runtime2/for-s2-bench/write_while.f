a := range(1000000)
i := 0
while (i < 1000000) { a[i] = a[i] + 1; i += 1 }
print(a[999999])
