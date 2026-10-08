a := []
i := 0
while(i < 8000) { a.push(i); i += 1 }
s := 0

i = 0
while(i < 8000) { s += a[i]; i += 1 }
print(s)

