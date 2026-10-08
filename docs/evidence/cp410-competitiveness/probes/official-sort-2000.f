a := []
i := 1
while(i <= 2000) { a.push(2000-i+1); i += 1 }
a = a.sort_by(x => x)
print(a[0]); print(a[2000-1]); print(a[1000-1])
