a := []
i := 0
while(i < 4000) { a.push(i); i += 1 }
s := 0

i = 0
while(i < 3992) { s += a[i+7] - a[i]; i += 1 }
print(s)

