a := []
i := 1
while(i <= 1000) { a.push(i); i += 1 }
best := 0
i = 0
while(i < 100) { best += a[i]; i += 1 }
cur := best
i = 100
while(i < 1000) { cur = cur - a[i - 100] + a[i]; if(cur > best) { best = cur }; i += 1 }
print(best)
