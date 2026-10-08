a := []
i := 0
while(i < 2000) { a.push(i); i += 1 }
s := 0

i = 0
while(i < 1992) { s += a[i+7] - a[i]; i += 1 }
print(s)

